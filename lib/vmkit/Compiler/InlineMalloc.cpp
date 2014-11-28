//===------------- InlineMalloc.cpp - Inline allocations  -----------------===//
//
//                              The VMKit project
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// A compiler pass to inline allocations and write barriers.
//===----------------------------------------------------------------------===//

#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "vmkit/JIT.h"

using namespace llvm;

namespace vmkit {
  class InlineMalloc : public FunctionPass {
    public:
      static char ID;
      InlineMalloc() : FunctionPass(ID) {}
      virtual const char* getPassName() const {
        return "Inline malloc and barriers";
      }

      virtual bool runOnFunction(Function &F);
    private:
  };
  char InlineMalloc::ID = 0;

  bool InlineMalloc::runOnFunction(Function& F) {
    Module* enclosingMod = F.getParent();

    Function* VTMalloc = enclosingMod->getFunction("VTgcmalloc");
    Function* vmkitMalloc = enclosingMod->getFunction("vmkitgcmalloc");

    Function* FieldWriteBarrier = enclosingMod->getFunction("fieldWriteBarrier");
    Function* ArrayWriteBarrier = enclosingMod->getFunction("arrayWriteBarrier");
    Function* NonHeapWriteBarrier = enclosingMod->getFunction("nonHeapWriteBarrier");

    bool Changed = false;
    const DataLayout *DL = enclosingMod->getDataLayout();
    for (Function::iterator BI = F.begin(), BE = F.end(); BI != BE; BI++) {
      BasicBlock *Cur = BI;
      for (BasicBlock::iterator II = Cur->begin(), IE = Cur->end(); II != IE;) {
        Instruction *I = II;
        II++;
        if (I->getOpcode() != Instruction::Call &&
            I->getOpcode() != Instruction::Invoke) {
          continue;
        }

        CallSite Call(I);
        Function* callee = Call.getCalledFunction();

        // For mallocs, we only inline if the first argument is constant.
        if (callee == VTMalloc ||
            callee == vmkitMalloc) {
          if (!isa<Constant>(Call.getArgument(0))) {
            continue;
          }
        } else if (callee != FieldWriteBarrier &&
                   callee != NonHeapWriteBarrier &&
                   callee != ArrayWriteBarrier) {
          continue;
        }

        InlineFunctionInfo IFI(NULL, DL);
        Changed |= InlineFunction(Call, IFI);
        break;
      }
    }

    return Changed;
  }

  FunctionPass* createInlineMallocPass() {
    return new InlineMalloc();
  }

}
