//
// Created by ubuntu on 2/1/18.
//

//#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include "llvm/Support/Casting.h"
#include "llvm/IR/Constant.h"

#include <limits>
#include <sstream>
#include <utility>
#include "NodeFactory.h"
#include "Common.h"
#include "Helper.h"

using namespace llvm;

const unsigned AndersNodeFactory::InvalidIndex = std::numeric_limits<unsigned int>::max();

AndersNodeFactory::AndersNodeFactory() {
    // Note that we can't use std::vector::emplace_back() here because AndersNode's constructors are private hence std::vector cannot see it

    // Node #0 is always the universal ptr: the ptr that we don't know anything about.
    nodes.push_back(AndersNode(AndersNode::VALUE_NODE, 0));
    // Node #1 is always the universal obj: the obj that we don't know anything about.
    nodes.push_back(AndersNode(AndersNode::OBJ_NODE, 1));
    // Node #2 always represents the null pointer.
    nodes.push_back(AndersNode(AndersNode::VALUE_NODE, 2));
    // Node #3 is the object that null pointer points to
    nodes.push_back(AndersNode(AndersNode::OBJ_NODE, 3));
    // Node #4 is the constaint int obj
    nodes.push_back(AndersNode(AndersNode::OBJ_NODE, 4));
    
    assert(nodes.size() == 5);
}

NodeIndex AndersNodeFactory::createValueNode(const Value* val) {
    unsigned nextIdx = nodes.size();
    nodes.push_back(AndersNode(AndersNode::VALUE_NODE, nextIdx, val));
    if (val != nullptr) {
        assert(!valueNodeMap.count(val) && "Trying to insert two mappings to revValueNodeMap!");
        valueNodeMap[val] = nextIdx;
    }

    return nextIdx;
}

NodeIndex AndersNodeFactory::createObjectNode(const Value* val) {
    unsigned nextIdx = nodes.size();
    nodes.push_back(AndersNode(AndersNode::OBJ_NODE, nextIdx, val));
    if (val != nullptr) {
        if (objNodeMap.count(val))
            return objNodeMap[val];
        objNodeMap[val] = nextIdx;
    }

    return nextIdx;
}

NodeIndex AndersNodeFactory::createObjectNode(const NodeIndex base, const unsigned offset) {
    assert(offset != 0);

    unsigned nextIdx = nodes.size();
    assert(nextIdx == base + offset);
    const Value *val = getValueForNode(base);
    nodes.push_back(AndersNode(AndersNode::OBJ_NODE, nextIdx, nullptr, offset));
    if(isa<Instruction>(val))
    {
        const Instruction *Ins = dyn_cast<Instruction>(val);
        //setCreationBBName(nextIdx, Ins->getParent()->getName().str());
    }
    return nextIdx;
}

NodeIndex AndersNodeFactory::getValueNodeFor(const Value* val) {
    if (const Constant* c = dyn_cast<Constant>(val))
        if (!isa<GlobalValue>(c))
        {
            //OP<<"!Global Value\n";
            return getValueNodeForConstant(c);
        }
            
    if (isa<GlobalValue>(val))
    {
        //OP<<"global val\n";
        const GlobalValue *globalVal = dyn_cast<GlobalValue>(val);
        if (globalVal -> isDeclaration())
        {
            auto itr = Ctx->Gobjs.find(globalVal->getName().str());
            if(!(itr == Ctx->Gobjs.end()))
                val = itr->second;
            else    
                return getUniversalPtrNode();
        }
    }
    auto itr = valueNodeMap.find(val);
    if (itr == valueNodeMap.end()) {
        //OP<<"invalidIndex\n";
        return InvalidIndex;
    } else {
        return itr->second;
    }
}

NodeIndex AndersNodeFactory::getValueNodeForConstant(const llvm::Constant* c) {
    if (const ConstantInt *CI = dyn_cast<ConstantInt>(c))
        return getConstantIntNode();
    if (const ConstantFP *CF = dyn_cast<ConstantFP>(c))
	return getConstantIntNode();
    //errs()<<"Inside getValueNodeForConstant.\n";
    //errs()<<"Constant: "<<*c<<"\n";
    //assert(isa<PointerType>(c->getType()) && "Not a constant pointer!");

    if (isa<ConstantPointerNull>(c) || isa<UndefValue>(c))
        return getNullPtrNode();
    else if (const GlobalValue* gv = dyn_cast<GlobalValue>(c))
    {
	//errs()<<"is Global Val, NodeIndex = "<<getValueNodeFor(gv)<<"\n";
	return getValueNodeFor(gv);
    }
    else if (const ConstantExpr* ce = dyn_cast<ConstantExpr>(c)) {
        switch (ce->getOpcode()){
            case Instruction::GetElementPtr: {
                NodeIndex baseNode = getValueNodeForConstant(ce->getOperand(0));
                assert(baseNode != InvalidIndex && "missing base val node for gep");

                if (baseNode == getNullObjectNode())
                    return getNullPtrNode();

                if (baseNode == getUniversalObjNode()) {
                    errs() << "GEP CE, universal obj " << *(ce->getOperand(0)) << "\n";
                    return getUniversalPtrNode();
                }

                unsigned fieldNum = constGEPtoFieldNum(ce);
		//OP<<"const is GEP, fieldNum = "<<fieldNum<<"\n";
                if (fieldNum == 0)
                    return baseNode;

                auto mapKey = std::make_pair(baseNode, fieldNum);
                auto itr = gepMap.find(mapKey);
                if (itr == gepMap.end()) {
		    
                    NodeIndex gepIndex = createValueNode(ce);
                    gepMap.insert(std::make_pair(mapKey, gepIndex));
                    gepNodeMap[gepIndex] = mapKey;
		    //errs()<<"getp end, nodeIndex= "<<gepIndex<<"\n";
                    return gepIndex;
                } else
                    return itr->second;
            }
            case Instruction::BitCast: {
                NodeIndex srcNode = getValueNodeFor(ce->getOperand(0));
                if (srcNode == getNullObjectNode())
                    return getNullPtrNode();

                if (srcNode == getUniversalObjNode()) {
                    errs() << "GEP CE, universal obj " << *(ce->getOperand(0)) << "\n";
                    return getUniversalPtrNode();
                }

                return srcNode;
            }
            case Instruction::IntToPtr:
                return getNullPtrNode();
            case Instruction::PtrToInt:
                return getNullPtrNode();
            default:
                errs() << "Constant Expr not yet handled: " << *ce << "\n";
                return getUniversalPtrNode();
                //llvm_unreachable(0);
        }
    } else if (isa<BlockAddress>(c)) {
        // FIXME return NULL now
        return getNullPtrNode();
    }

    std::string insStr;
    llvm::raw_string_ostream rso(insStr);
    c->print(rso);
    if (insStr.find("zeroinitializer")!=std::string::npos) {
    	return getConstantIntNode();
    }
    errs() << "Unknown constant pointer: " << *c << "\n";
    llvm_unreachable("Unknown constant pointer!");
    return InvalidIndex;
}

NodeIndex AndersNodeFactory::getObjectNodeFor(const Value* val) {
    if(const Constant* c = dyn_cast<const Constant>(val)){
        if(!isa<GlobalValue>(c))
            return getObjectNodeForConstant(c);

        const GlobalValue* gval = dyn_cast<GlobalValue>(c);
        if (gval && gval->isDeclaration()) {
            if (isa<GlobalVariable>(gval)) {
                auto itr = gobjMap->find(gval->getName());
                if(itr != gobjMap->end()) {
                    val = itr->second;
                }
            } else if (isa<Function>(gval)) {
                auto itr = funcMap->find(gval->getName());
                if (itr != funcMap->end())
                    val = itr->second;
            }
        }
    }

    auto itr = objNodeMap.find(val);
    if (itr == objNodeMap.end())
        return InvalidIndex;
    else
        return itr->second;
}

NodeIndex AndersNodeFactory::getObjectNodeForConstant(const llvm::Constant* c) {
    //assert(isa<PointerType>(c->getType()) && "Not a constant pointer!");
    if(!isa<PointerType>(c->getType())) {
        return getUniversalPtrNode();
    }
    if (isa<ConstantPointerNull>(c))
        return getNullObjectNode();
    else if (const GlobalValue* gv = dyn_cast<GlobalValue>(c))
        return getObjectNodeFor(gv);
    else if (const ConstantExpr* ce = dyn_cast<ConstantExpr>(c)) {
        switch (ce->getOpcode()) {
            case Instruction::GetElementPtr: {
                NodeIndex baseNode = getObjectNodeForConstant(ce->getOperand(0));
                assert(baseNode != InvalidIndex && "missing base obj node for gep");
                if (baseNode == getNullObjectNode() || baseNode == getUniversalObjNode())
                    return baseNode;

                return getOffsetObjectNode(baseNode, constGEPtoFieldNum(ce));
            }
            case Instruction::IntToPtr:
                //errs() << "IntToPtr CE " << *c << "-> UniversalObj\n";
                //return getUniversalObjNode();
                return getNullObjectNode();
            case Instruction::PtrToInt:
                //errs() << "PtrToInt CE " << *c << "-> UniversalObj\n";
                //return getUniversalObjNode();
                return getNullObjectNode();
            case Instruction::BitCast:
                return getObjectNodeForConstant(ce->getOperand(0));
            default:
                errs() << "Constant Expr not yet handled: " << *ce << "\n";
                llvm_unreachable(0);
        }
    } else if (isa<BlockAddress>(c)) {
        // FIXME return NULL now
        return getNullObjectNode();
    }

    errs() << "Unknown constant pointer: " << *c << "\n";
    llvm_unreachable("Unknown constant pointer!");
    return InvalidIndex;
}

NodeIndex AndersNodeFactory::getReturnNodeFor(const llvm::Function* f) {
    auto itr = returnMap.find(f);
    if (itr == returnMap.end())
        return InvalidIndex;
    else
        return itr->second;
}

NodeIndex AndersNodeFactory::getVarargNodeFor(const llvm::Function* f) {
    auto itr = varargMap.find(f);
    if (itr == varargMap.end())
        return InvalidIndex;
    else
        return itr->second;
}

unsigned AndersNodeFactory::constGEPtoFieldNum(const llvm::ConstantExpr* expr) const {
    assert(expr->getOpcode() == Instruction::GetElementPtr && "constGEPtoVariable receives a non-gep expr!");

    unsigned offset = getGEPOffset(expr, dataLayout);
    return offsetToFieldNum(GetUnderlyingObject(expr, *dataLayout, 0), offset, dataLayout, structAnalyzer, module);
}

static void dumpLocation(const Value *val) {
    FUNCTION_TIMER();

    if (!val)
        return;

    if (const Instruction *inst = dyn_cast<Instruction>(val)) {
        DebugLoc loc = inst->getDebugLoc();
        if (isa<AllocaInst>(inst)) {
            std::string a;
            raw_string_ostream ao(a);
            inst->getType()->print(ao);
            ao << " %" << inst->getName();
            for (auto const& i : *(inst->getParent())) {
                if (const CallInst *ci = dyn_cast<CallInst>(&i)) {
                    Function *f = ci->getCalledFunction();
                    if (f != nullptr && !f->getName().compare("llvm.dbg.value")) {
                        std::string m;
                        raw_string_ostream mo(m);
                        ci->getOperand(0)->print(mo);
                        if (ao.str() == mo.str()) {
                            loc = ci->getDebugLoc();
                            break;
                        }
                    }
                }
            }
        }
        /*AA_LOG("\tsrc> ");
        const Function *F = inst->getParent()->getParent();
        if (F && F->hasName())
            AA_LOG(" (" << F->getName() << ") ");
        if (VerboseLevel >= 2)
            loc.print(errs());
        AA_LOG("\n");*/
    }
}


void AndersNodeFactory::dumpNode(NodeIndex idx) const {

    const AndersNode& n = nodes.at(idx);

    if (n.type == AndersNode::VALUE_NODE)
        OP<<"V ";
    else if (n.type == AndersNode::OBJ_NODE)
        OP<<"O ";
    else
        assert(false && "Wrong type number!");
    OP<<"#" << n.idx << "\t";
    /*if (n.type == AndersNode::VALUE_NODE)
        AA_LOG("V ");
    else if (n.type == AndersNode::OBJ_NODE)
        AA_LOG("O ");
    else
        assert(false && "Wrong type number!");
    AA_LOG("#" << n.idx << "\t");*/

    // Dump node value info.
    const Value* val = n.getValue();
    if (val == nullptr) {
        NodeIndex offset = n.getOffset();
        if (offset == 0)
           OP<<"nullptr>";
        else
        {
            NodeIndex baseIdx = n.getIndex() - offset;
            const Value* base = nodes.at(baseIdx).getValue();
            assert(base != nullptr);

            OP<<"field [" << offset << "] of ";

            Type *BaseTy = base->getType();
            //if (BaseTy && VerboseLevel >= 2)
               // BaseTy->print(errs());

            if (base->hasName())
                OP<<" : " << base->getName();
        }
    }
    else if (isa<Function>(val))
        OP<<"f> " << val->getName();
    else
        OP<<"v> " << *val;
    OP<<"\n";

    // Dump source loc info if possible.
    dumpLocation(val);
}

void AndersNodeFactory::dumpNode(NodeIndex idx,
                                 std::map<NodeIndex, AndersPtsSet>& ptsGraph,
                                 std::set<NodeIndex>& dumped, bool dumpDep) const {

    dumpNode(idx);
    dumped.insert(idx);

    // Dump ptr set info.
    dumpNodePtrSetInfo(idx, ptsGraph, dumped, dumpDep);
}

static unsigned ptrMax;
static unsigned long ptrTotal;
static unsigned long ptrNumber;

void AndersNodeFactory::dumpNodePtrSetInfo(
        NodeIndex index, std::map<NodeIndex, AndersPtsSet>& ptsGraph,
        std::set<NodeIndex>& dumped, bool dumpDep) const {

    FUNCTION_TIMER();

    NodeIndex rep = getMergeTarget(index);
    //if (rep != index)
        //AA_LOG("\tmerge> " << index << " -> " << rep << "\n");

    auto ptsItr = ptsGraph.find(rep);
    if (ptsItr != ptsGraph.end()) {
        unsigned size = ptsItr->second.getSize();
        if (index != 0 && ptsItr->second.has(getUniversalObjNode()))
            outs() << "-1\n";
        else
            outs() << size <<"\n";
        if (size > ptrMax)
            ptrMax = size;

        ptrTotal += size;
        ptrNumber++;

        /*AA_LOG("\tptrs> ");
        for (auto v: ptsItr->second)
            AA_LOG(v << " ");
        AA_LOG("\n");*/

        if (dumpDep) {
            // Since we may not dump all the nodes
            // this is necessary for dumping the dependents
            for (auto v: ptsItr->second) {
                if (!dumped.count(v))
                    dumpNode(v, ptsGraph, dumped, dumpDep);
            }
        }
    }
}

void AndersNodeFactory::dumpNodeInfo(
        std::map<NodeIndex, AndersPtsSet>& ptsGraph,
        std::set<const Value*>* inclusion) const {
    FUNCTION_TIMER();
    std::set<NodeIndex> dumped;
    bool dumpDep = inclusion ? true : false;
    ptrMax = 0;
    ptrTotal = ptrNumber = 0;

    //AA_LOG("\n----- Print AndersNodeFactory Info -----\n");
    for (auto const& node: nodes)
    {
        // Dump node ordinal info.
        NodeIndex index = node.getIndex();
        const Value* val = node.getValue();

        // Only dump the requested value if provided
        if (inclusion != nullptr && !inclusion->count(val))
            continue;

        // Do not re-dump
        if (dumped.count(index))
            continue;

        dumpNode(index, ptsGraph, dumped, dumpDep);
    }

    /*AA_LOG("\nReturn Map:\n");
    for (auto const& mapping: returnMap)
        AA_LOG(mapping.first->getName() << "  -->>  [Node #" << mapping.second << "]\n");

    AA_LOG("\nVararg Map:\n");
    for (auto const& mapping: varargMap)
        AA_LOG(mapping.first->getName() << "  -->>  [Node #" << mapping.second << "]\n");
    AA_LOG("----- End of Print -----\n");*/

    errs() << "\nStatistic Info:\n";
    errs() << "ptrMax = " << ptrMax << "\n";
    errs() << "ptrTotal = " << ptrTotal << "\n";
    errs() << "ptrNumber = " << ptrNumber << "\n";
}

NodeIndex AndersNodeFactory::getMergeTarget(NodeIndex n) {
    assert(n < nodes.size());
    NodeIndex ret = nodes[n].mergeTarget;
    if (ret != n)
    {
        std::vector<NodeIndex> path(1, n);
        while (ret != nodes[ret].mergeTarget)
        {
            path.push_back(ret);
            ret = nodes[ret].mergeTarget;
        }
        for (auto idx: path)
            nodes[idx].mergeTarget = ret;
    }
    assert(ret < nodes.size());
    return ret;
}

NodeIndex AndersNodeFactory::getMergeTarget(NodeIndex n) const {
    assert (n < nodes.size());
    NodeIndex ret = nodes[n].mergeTarget;
    while (ret != nodes[ret].mergeTarget)
        ret = nodes[ret].mergeTarget;
    return ret;
}

NodeIndex AndersNodeFactory::createVarargNode(const llvm::Function* f) {
    unsigned nextIdx = nodes.size();
    nodes.push_back(AndersNode(AndersNode::OBJ_NODE, nextIdx, f));

    assert(!varargMap.count(f) && "Trying to insert two mappings to varargMap!");
    varargMap[f] = nextIdx;

    return nextIdx;
}

NodeIndex AndersNodeFactory::createReturnNode(const llvm::Function* f) {
    unsigned nextIdx = nodes.size();
    nodes.push_back(AndersNode(AndersNode::VALUE_NODE, nextIdx, f));

    assert(!returnMap.count(f) && "Trying to insert two mappings to returnMap!");
    returnMap[f] = nextIdx;

    return nextIdx;
}
