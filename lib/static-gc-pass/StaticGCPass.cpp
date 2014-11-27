//===---- StaticGCPass.cpp - Put GC information in functions compiled --------//
//===----------------------- with llvm-gcc --------------------------------===//
//
//                     The VMKit project
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass ensures that every function making use of the gcroot intrinsic
// has had its GC set to something sensible, and ensures that no function 
// with static linkage attempts to use gcroot.
//
//===----------------------------------------------------------------------===//


#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>

using namespace llvm;

namespace {

  class StaticGCPass : public ModulePass {
  public:
    static char ID;
    
    StaticGCPass() : ModulePass(ID) {}

    virtual bool runOnModule(Module& M);

    // This pass has no interesting interactions with other passes.
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
    } 

  };

  char StaticGCPass::ID = 0;
  RegisterPass<StaticGCPass> X("StaticGCPass", "Add GC information in files compiled with llvm-gcc");

  bool StaticGCPass::runOnModule(Module& M) {
    Function* F = M.getFunction("__llvm_gcroot");
    Function *gcrootFun = Intrinsic::getDeclaration(&M, Intrinsic::gcroot);

    if (F) {
      F->replaceAllUsesWith(gcrootFun);
      F->eraseFromParent();
    }

    // Find every use of the gcroot intrinsic and ensure that the containing Function has
    // had its GC set to something meaningful.
    bool error = false;
    for (Value::use_iterator I = gcrootFun->use_begin(), E = gcrootFun->use_end(); I != E; ++I) {
      // Extract the User from the Use.
      Instruction* II = dyn_cast<Instruction>(const_cast<User*>(I->getUser()));
      if (!II) {
        fprintf(stderr, "Unable to cast use to an Instruction... Did LLVM change?");
        continue;
      }

      // The first parent is the basic block, which itself has the Function as parent.
      Function* F = II->getParent()->getParent();
      if (!F->hasGC()) {
        F->setGC("vmkit");
      }
    }

    for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I) {
      if (I->hasGC() && I->hasInternalLinkage()) {
        error = true;
        fprintf(stderr, "Method %s has static linkage but uses gc_root. "
                        "Functions using gc_root should not have static linkage.\n",
                        I->getName().data());
      }
    }

    if (error) {
      abort();
    }

    return true;
  }

}
