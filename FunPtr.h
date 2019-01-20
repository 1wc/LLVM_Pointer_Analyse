#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/IR/InstIterator.h>
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "Dataflow.h"
#include "llvm/IR/DebugInfo.h"
using namespace llvm;

struct FunPtrInfo {
    std::map<Value *, std::set<Value *>> PointTos;
    FunPtrInfo() : PointTos() {};
    FunPtrInfo(const FunPtrInfo & info) : PointTos(info.PointTos) {}

    bool operator == (const FunPtrInfo & info) const {
        return PointTos == info.PointTos;
    }
};

inline raw_ostream &operator<<(raw_ostream &out, const FunPtrInfo &info) {
    for (std::map<Value *, std::set<Value *>>::const_iterator it = info.PointTos.begin();
        it != info.PointTos.end(); ++it) {
        const Value *Pointer = it->first;
        std::set<Value *> Pointees = it->second;
        out <<"\nPointer is:\n";
        // out << *(Pointer) << "\n";
        if (Pointer == NULL) {
            out <<"is NULL";
        }else{
            out << (Pointer) << (*Pointer)<<"\n";
            out <<"\nValues are:\n";
            for (std::set<Value *>::iterator tmpit = Pointees.begin(); tmpit != Pointees.end();
                ++tmpit)  {
                // out << *(*tmpit) << " ";
                out << (*tmpit)<< (**tmpit)<<" ";
            }
            out <<"\noverover\n";
        }
    }
    return out;
}
std::set<CallInst *> directCalls;
std::map<CallInst *, std::set<Function *>> indirectCalls;
std::map<Function *, FunPtrInfo> worklist;

// store the data flow after function iterator.
std::map<Function * ,std::map<Value *, std::set<Value *>>> GPointTos;
// std::map<Function *, std::map<Value *, std::set<Value *>>> extraPointTos;

int it_time = 0;

class FunPtrVisitor : public DataflowVisitor<struct FunPtrInfo>{

public:
    FunPtrVisitor() {}
    void merge(FunPtrInfo * dest, const FunPtrInfo & src) override {
        for (std::map<Value *, std::set<Value *>>::const_iterator ptiter = src.PointTos.begin();
            ptiter != src.PointTos.end(); ptiter++) {
            dest->PointTos[(*ptiter).first].insert((*ptiter).second.begin(),(*ptiter).second.end());
       }
    }
    void mergeVal(FunPtrInfo *dest, FunPtrInfo * src) {
        typedef std::map<Value *, std::set<Value *>> pointersty;

        pointersty map1 = dest->PointTos;
        pointersty map2 = src->PointTos;

        for (pointersty::iterator pit = map2.begin(); pit != map2.end(); pit++) {
            if (map1.find((*pit).first) != map1.end()) {
                map1[(*pit).first] = (*pit).second;
            } else {
                map1[(*pit).first].insert((*pit).second.begin(), (*pit).second.end());
            }
        }
    }

    std::map<Function* ,std::set<Value *>> getRetVal(CallInst *callin, FunPtrInfo * dfval){
        std::map<Function* ,std::set<Value *>> retmap;
        if(callin->getCalledFunction() != NULL){
            Function* callfunc = callin->getCalledFunction();
            Value *retval;
            for (inst_iterator ii = inst_begin(callfunc); ii != inst_end(callfunc); ++ii) {
                if (ReturnInst *ri = dyn_cast<ReturnInst>(&*ii)) {
                    retval = ri->getReturnValue();
                    retmap[callfunc].insert(retval);
                    break;
                }
            }

        } else {
            Value * callf = callin->getCalledValue();
            for (std::set<Value *>::iterator it = dfval->PointTos[callf].begin(); 
                it != dfval->PointTos[callf].end(); it++) {
                Value *retval;
                if (Function *func = dyn_cast<Function>(*it)) {
                    for (inst_iterator ii = inst_begin(func); ii != inst_end(func); ++ii) {
                        if (ReturnInst *ri = dyn_cast<ReturnInst>(&*ii)) {
                        retval = ri->getReturnValue();
                        retmap[func].insert(retval);
                        break;
                        }
                    }
                }
            }
        }
        return retmap;
    }

    void compDFVal(Instruction *inst, FunPtrInfo *dfval ) override {
        if (isa<DbgInfoIntrinsic>(inst)) return;
        if (isa<MemIntrinsic>(inst)) return;

        if (CallInst *CI = dyn_cast<CallInst>(inst)) {
            // process direct call inst
            if (CI->getCalledFunction() != NULL) {
                directCalls.insert(CI);
                // process args passed
                Function *callee = CI->getCalledFunction();
                
                for (unsigned i = 0;i < CI->getNumArgOperands(); ++i) {
                    Value *y = CI->getArgOperand(i);

                    Function::arg_iterator argit = callee->arg_begin();
                    argit += i;
                    Value *x = &*argit;
                    // x xingcan
                    // y shican
                    if (PHINode *Phi = dyn_cast<PHINode>(y)) {
                        dfval->PointTos[x].insert(dfval->PointTos[y].begin(), 
                            dfval->PointTos[y].end());
                        worklist[callee].PointTos[x].insert(dfval->PointTos[x].begin(), 
                            dfval->PointTos[x].end());
                        // insert pointer's pointer into worklist
                        for (std::set<Value *>::iterator vit = dfval->PointTos[y].begin();
                            vit != dfval->PointTos[y].end(); vit++) {
                            worklist[callee].PointTos[*vit].insert(dfval->PointTos[*vit].begin(),
                                dfval->PointTos[*vit].end());
                        }
                        
                    } else if (x->getType()->isPointerTy()) {
                        for(std::set<Value *>::iterator tmpit = dfval->PointTos[y].begin();
                            tmpit != dfval->PointTos[y].end(); tmpit++) {
                            worklist[callee].PointTos[*tmpit].insert(dfval->PointTos[*tmpit].begin(),
                                dfval->PointTos[*tmpit].end());
                        }
                        worklist[callee].PointTos[x].insert(y);
                    }
                    worklist[callee].PointTos[x].insert(dfval->PointTos[y].begin(),
                        dfval->PointTos[y].end());
                }
                
                // TODO
                // test for case 26
                // pass all the dataflow value to callee and into GPointTos
                for (std::map<Value *, std::set<Value *>>::iterator it = dfval->PointTos.begin();
                    it != dfval->PointTos.end(); it++) {
                    worklist[callee].PointTos[it->first].insert(it->second.begin(),
                        it->second.end());
                }


            } else {
                // else, process undirect call inst
                Value *pvv = CI->getCalledValue();
                std::set<Function *> tmpset;
                std::map<Function* ,std::set<Value *>> retmap;
                for (std::set<Value *>::iterator it = dfval->PointTos[pvv].begin(); 
                    it != dfval->PointTos[pvv].end(); it++) {
                    // errs()<<**it<<"\n";
                    if (Function *func = dyn_cast<Function>(*it)) {
                        tmpset.insert(func);
                    } else {
                        // tricky
                        for (std::set<Value *>::iterator cddit = dfval->PointTos[pvv].begin();
                            cddit != dfval->PointTos[pvv].end(); cddit++) {
                            if (dfval->PointTos.find(*cddit) != dfval->PointTos.end() && 
                                dfval->PointTos[*cddit].size() > 0) {
                                for (std::set<Value *>::iterator vit = dfval->PointTos[*cddit].begin();
                                    vit != dfval->PointTos[*cddit].end(); vit++) {
                                    if (Function *func = dyn_cast<Function>(*vit)) {
                                        tmpset.insert(func);
                                    }
                                }
                            }
                        }
                    }
                }
                // errs()<<"over\n\n";
                for (std::set<Value *>::iterator it = dfval->PointTos[pvv].begin(); 
                it != dfval->PointTos[pvv].end(); it++) {
                    if (Function *callee = dyn_cast<Function>(*it)){
                        for (unsigned i = 0;i < CI->getNumArgOperands(); ++i) {
                            Value *y = CI->getArgOperand(i);    
                            Function::arg_iterator argit = callee->arg_begin();
                            argit += i;
                            Value *x = &*argit;
                            // x xingcan
                            // y shican
                            if (PHINode *Phi = dyn_cast<PHINode>(y)) {
                                // TODO ???
                                dfval->PointTos[x].insert(dfval->PointTos[y].begin(), 
                                    dfval->PointTos[y].end());
                                worklist[callee].PointTos[x].insert(dfval->PointTos[x].begin(), 
                                    dfval->PointTos[x].end());
                            } else if (isa<AllocaInst>(y)) {
                                worklist[callee].PointTos[x].insert(dfval->PointTos[y].begin(),
                                        dfval->PointTos[y].end());
                                
                            } else if (x->getType()->isPointerTy()) {
                                worklist[callee].PointTos[x].insert(y);
                            }
                            worklist[callee].PointTos[x].insert(dfval->PointTos[y].begin(),
                                dfval->PointTos[y].end());
                        }
                    }
                }
                if (indirectCalls.find(CI) != indirectCalls.end()) {
                    indirectCalls[CI].insert(tmpset.begin(), tmpset.end());
                } else {
                    indirectCalls[CI] = tmpset;
                }

            }
            std::map<Function* ,std::set<Value *>> retmap;
            retmap = getRetVal(CI, dfval);
            for(std::map<Function* ,std::set<Value *>>::iterator it = retmap.begin(); it!=retmap.end(); it++){
                for(std::set<Value *>::iterator init = ((*it).second).begin(); init != ((*it).second).end(); init++){
                    for (std::set<Value *>::iterator tit = worklist[(*it).first].PointTos[*init].begin();
                        tit != worklist[(*it).first].PointTos[*init].end(); tit++) {
                    }
                    dfval->PointTos[CI].insert(worklist[(*it).first].PointTos[*init].begin(),
                        worklist[(*it).first].PointTos[*init].end());
                }
            }

            if (CI->getCalledFunction() != NULL) {
                Function *callee = CI->getCalledFunction();
                for (unsigned i = 0;i < CI->getNumArgOperands(); ++i) {
                    Value *y = CI->getArgOperand(i);
                    
                    if(GPointTos[callee].find(y)!=GPointTos[callee].end()){
                        dfval->PointTos[y].clear();
                        dfval->PointTos[y].insert(GPointTos[callee][y].begin(), 
                            GPointTos[callee][y].end());
                    } else {
                        dfval->PointTos[y].clear();
                    }
                }
                

                for (std::map<Value *, std::set<Value *>>::iterator it = GPointTos[callee].begin();
                    it != GPointTos[callee].end(); it++) {
                    // errs()<<"point is "<<*(it->first)<<"\n";
                    dfval->PointTos[it->first].clear();
                    // errs()<<"values are\n";
                    // for (std::set<Value *>::iterator iit = it->second.begin(); iit != it->second.end(); iit++) {
                    //     errs()<<**iit<<"\n";
                    // }
                    // errs()<<"end for point "<<*(it->first)<<"\n\n";
                    dfval->PointTos[it->first].insert(it->second.begin(), it->second.end());
                }
                // pass all the dfval
                // For case 26



                // for(std::map<Value *,std::set<Value *>>::iterator it = dfval->PointTos.begin(); 
                //     it!= dfval->PointTos.end(); it++){
                //     if(GPointTos[callee].find((*it).first)!=GPointTos[callee].end()){
                //         dfval->PointTos[(*it).first].clear();
                //         dfval->PointTos[(*it).first].insert(GPointTos[callee][(*it).first].begin(), 
                //             GPointTos[callee][(*it).first].end());
                //     } else {
                //         dfval->PointTos[(*it).first].clear();
                //     }
                // } 

                // errs()<<"overover\n---------------------------------\n";
            }
        } else if (ReturnInst *Ri = dyn_cast<ReturnInst>(inst)) {
            if (dfval->PointTos[Ri->getReturnValue()].size() > 0) {
                for (std::map<CallInst *, std::set<Function *>>::iterator cit = indirectCalls.begin();
                    cit != indirectCalls.end(); cit++) {
                    for (std::set<Function *>::iterator fit = cit->second.begin();
                        fit != cit->second.end(); fit++) {
                        if (*fit == Ri->getParent()->getParent()) {
                            // TODO, now we solve this issue by a trick.
                            it_time += 1;
                            if (it_time < 5) {
                                worklist[cit->first->getParent()->getParent()].PointTos[cit->first].insert(
                                    dfval->PointTos[Ri->getReturnValue()].begin(),dfval->PointTos[Ri->getReturnValue()].end());
                            }
                        }
                    }
                }
            }

            // iterate functions to find the caller
            // now we should search on not only directCall but also indirectCall.
            // errs()<<"circle start\n";
            for (std::set<CallInst *>::iterator cit = directCalls.begin(); cit != directCalls.end(); cit++) {
                // if the real arg's data flow value have been changed, pass is to the callee and insert it 
                // into worklist.
                Function *caller = (*cit)->getParent()->getParent();
                Function *callee = (*cit)->getCalledFunction();
                if (callee == Ri->getParent()->getParent()) {
                    for (unsigned i = 0;i < (*cit)->getNumArgOperands(); ++i) {
                        Value *y = (*cit)->getArgOperand(i);    
                        Function::arg_iterator argit = callee->arg_begin();
                        argit += i;
                        Value *x = &*argit;
                        // x xingcan
                        // y shican
                        if (dfval->PointTos.find(x) != dfval->PointTos.end()) {

                            // Function *fun = Ri->getParent()->getParent();
                            GPointTos[callee][y].insert(dfval->PointTos[x].begin(),dfval->PointTos[x].end());  
                            it_time += 1;
                            if (it_time < 5) {

                                // TODO

                                // FunPtrInfo initval;
                                // worklist[caller] = initval;
                                for (std::map<Value *, std::set<Value *>>::iterator mapit = dfval->PointTos.begin();
                                    mapit != dfval->PointTos.end(); mapit++) {
                                    if (mapit->first == x) {
                                        continue;
                                    }
                                    worklist[caller].PointTos[mapit->first].insert(mapit->second.begin(),
                                        mapit->second.end());
                                }
                            }
                        }
                    }
                    // we should pass all the dfval into GPoint except the xingcan
                    // Too much trouble

                    // for (std::map<Value *, std::set<Value *>>::iterator it = dfval->PointTos.begin();
                    //     it != dfval->PointTos.end(); it++) {
                    //     int flag = 0;
                    //     for (unsigned i = 0;i < (*cit)->getNumArgOperands(); ++i) {   
                    //         Function::arg_iterator argit = callee->arg_begin();
                    //         argit += i;
                    //         Value *x = &*argit;
                    //         if (it->first == x) {
                    //             flag = 1;
                    //             break;
                    //         }
                    //     }
                    //     if (!flag) {
                    //         GPointTos[callee][it->first].insert(it->second.begin(),it->second.end());
                    //     }
                    // }
                }
            }
        } else if (PHINode *Phi = dyn_cast<PHINode>(inst)) {
            unsigned num = Phi->getNumIncomingValues();
            for (unsigned i = 0; i < num; ++i) {
                Value *v = Phi->getIncomingValue(i);
                dfval->PointTos[Phi].insert(v);

                if (PHINode *inPhi = dyn_cast<PHINode>(v)) {
                    for (unsigned j = 0;j < inPhi->getNumIncomingValues(); ++j) {
                        if (Function *func = dyn_cast<Function>(inPhi->getIncomingValue(j))) {
                            dfval->PointTos[Phi].insert(inPhi->getIncomingValue(j));
                        }
                    }
                }
            }
        } else if (GetElementPtrInst *Gep = dyn_cast<GetElementPtrInst>(inst)) {
            
            if (Gep->isInBounds()) {

                Value *strc = Gep->getPointerOperand();
                dfval->PointTos[Gep].insert(strc);
            }
        } else if (StoreInst *Si = dyn_cast<StoreInst>(inst)) {
            Value *Vop = Si->getValueOperand();
            Value *Pop = Si->getPointerOperand();
            if (isa<Function>(Vop)) {
                if (isa<BitCastInst>(Pop)) {
                    dfval->PointTos[Pop].clear();
                    dfval->PointTos[Pop].insert(Vop);
                } else {
                    std::set<Value *> tmpset = dfval->PointTos[Pop];
                    for (std::set<Value *>::iterator tmpit = tmpset.begin(); tmpit != tmpset.end();
                        tmpit++) {

                        // *tmpit = %24
                        // %24 -> %4
                        // for case 26's else branch

                        if (LoadInst *li = dyn_cast<LoadInst>(*tmpit)) {

                            for (std::set<Value *>::iterator it = dfval->PointTos[li].begin();
                                it != dfval->PointTos[li].end(); it++) {
                                if (isa<AllocaInst>(*it)) {
                                    for (std::set<Value *>::iterator iit = dfval->PointTos[*it].begin();
                                        iit != dfval->PointTos[*it].end(); iit++) {
                                        dfval->PointTos[*iit].clear();
                                        dfval->PointTos[*iit].insert(Vop);

                                    }
                                }
                            }
                        }
                        else {
                            dfval->PointTos[*tmpit].clear();
                            dfval->PointTos[*tmpit].insert(Vop);
                        } 
                        
                    } 
                }
                
            } else {
                if (isa<AllocaInst>(Vop)) {
                    std::set<Value *> tmpset = dfval->PointTos[Pop];
                    for (std::set<Value *>::iterator tmpit = tmpset.begin(); tmpit != tmpset.end();
                        tmpit++) {
                        dfval->PointTos[*tmpit].clear();
                        dfval->PointTos[*tmpit].insert(Vop);
                    } 
                } else {
                    std::set<Value *> should_insert;
                    // TODO
                    // for case 26 if branch
                    // Vop = %6
                    // %6 -> %5'

                    if (LoadInst *li = dyn_cast<LoadInst>(Vop)) {
                        for (std::set<Value *>::iterator it = dfval->PointTos[li].begin();
                            it != dfval->PointTos[li].end(); it++) {
                            if (isa<AllocaInst>(*it)) {
                                for (std::set<Value *>::iterator iit = dfval->PointTos[*it].begin();
                                    iit != dfval->PointTos[*it].end(); iit++) {
                                    if (dfval->PointTos[*iit].begin() != dfval->PointTos[*iit].end())
                                        should_insert = dfval->PointTos[*iit];
                                }
                            }
                        }
                    }
                    
                    std::set<Value *> tmpset = dfval->PointTos[Pop];
                    for (std::set<Value *>::iterator tmpit = tmpset.begin(); tmpit != tmpset.end();
                        tmpit++) {
                        // *tmpit = %10
                        // %10 -> %7' bitcast
                        
                        if (LoadInst *li = dyn_cast<LoadInst>(*tmpit)) {
                            for (std::set<Value *>::iterator it = dfval->PointTos[li].begin();
                                it != dfval->PointTos[li].end(); it++) {
                                if (isa<BitCastInst>(*it)) {
                                    
                                    for (std::set<Value *>::iterator iit = dfval->PointTos[*it].begin();
                                        iit != dfval->PointTos[*it].end(); iit++) {
                                        for (std::set<Value *>::iterator iiit = dfval->PointTos[*iit].begin();
                                            iiit != dfval->PointTos[*iit].end(); iiit++) {

                                            dfval->PointTos[*iiit].clear();
                                            dfval->PointTos[*iiit].insert(should_insert.begin(),
                                                should_insert.end());
                                            GPointTos[Si->getParent()->getParent()][*iiit].insert(dfval->PointTos[*iiit].begin(),
                                                dfval->PointTos[*iiit].end());
                                        }
                                    }
                                }
                            }


                        } else {
                            dfval->PointTos[*tmpit].clear();
                            dfval->PointTos[*tmpit].insert(dfval->PointTos[Vop].begin(),
                                dfval->PointTos[Vop].end());
                        }

                        
                    } 
                }        
            }
        } else if (LoadInst *Li = dyn_cast<LoadInst>(inst)){
            Value *Pop = Li->getPointerOperand();
            if (isa<BitCastInst>(Pop)) {
                dfval->PointTos[Li].insert(dfval->PointTos[Pop].begin(),
                    dfval->PointTos[Pop].end());
            } else {
                std::set<Value *> tmpset = dfval->PointTos[Pop];
                for (std::set<Value *>::iterator tmpit = tmpset.begin(); tmpit != tmpset.end();
                    tmpit++) {
                    dfval->PointTos[Li].insert(dfval->PointTos[*tmpit].begin(),
                        dfval->PointTos[*tmpit].end());
                }
            }
        } 

    }

};

///!TODO TO BE COMPLETED BY YOU FOR ASSIGNMENT 3
class FuncPtrPass : public ModulePass {
public:
    static char ID; // Pass identification, replacement for typeid
    FuncPtrPass() : ModulePass(ID) {}

    void printRes() {
        std::map<unsigned, std::set<Function *>> finalres;
        for (std::set<CallInst *>::iterator it = directCalls.begin();
            it != directCalls.end(); it++) {
            finalres[getLineNo(*it)].insert((*it)->getCalledFunction());
        }

        for (std::map<CallInst *, std::set<Function *>>::iterator it = indirectCalls.begin();
            it != indirectCalls.end(); it++) {
            finalres[getLineNo(it->first)].insert(it->second.begin(), it->second.end());
        }

        for (std::map<unsigned, std::set<Function *>>::iterator resit = finalres.begin();
            resit != finalres.end(); resit++) {
            errs()<<resit->first<<" : ";
            int flag = 1;
            for (std::set<Function *>::iterator funcit = resit->second.begin();
                funcit != resit->second.end(); funcit++) {
                if (flag == 1) {
                    errs()<<(*funcit)->getName();
                    flag = 0;
                } else {
                    errs()<<", "<<(*funcit)->getName();
                }
            }
            errs()<<"\n";
        }
    }
    unsigned getLineNo(Instruction *I) {
        unsigned line;
        if (DILocation *Loc = I->getDebugLoc()) {
            line = Loc->getLine();
            std::string filename = Loc->getFilename().str();

        if (filename.empty()) {
            DILocation *oDILoc = Loc->getInlinedAt();
        if (oDILoc) {
            line = oDILoc->getLine();
        }
      }
    }
    return line;
  }
    bool runOnModule(Module &M) override {

        // M.print(errs(), 0);
        for (Function &F : M) {
            FunPtrInfo initval;
            worklist[&F] = initval;
        }
        int cnt = 0;
        while (worklist.size() > 0) {
            for(std::map<Function *, FunPtrInfo>::iterator it = worklist.begin() ; it != worklist.end() ; it++){
                // errs()<<"deal with "<<it->first->getName()<<"\n";
                // errs()<< "--------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n";
                FunPtrVisitor visitor;
                DataflowResult<FunPtrInfo>::Type result;
                compForwardDataflow(((*it).first), &visitor, &result, (*it).second);
                // printDataflowResult<FunPtrInfo>(errs(), result);
                worklist.erase(it);
            }
        }
        printRes();
        return false;
    }
};
char FuncPtrPass::ID = 0;