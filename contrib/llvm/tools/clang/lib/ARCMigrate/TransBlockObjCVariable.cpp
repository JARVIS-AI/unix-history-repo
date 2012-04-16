//===--- TransBlockObjCVariable.cpp - Tranformations to ARC mode ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// rewriteBlockObjCVariable:
//
// Adding __block to an obj-c variable could be either because the the variable
// is used for output storage or the user wanted to break a retain cycle.
// This transformation checks whether a reference of the variable for the block
// is actually needed (it is assigned to or its address is taken) or not.
// If the reference is not needed it will assume __block was added to break a
// cycle so it will remove '__block' and add __weak/__unsafe_unretained.
// e.g
//
//   __block Foo *x;
//   bar(^ { [x cake]; });
// ---->
//   __weak Foo *x;
//   bar(^ { [x cake]; });
//
//===----------------------------------------------------------------------===//

#include "Transforms.h"
#include "Internals.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace arcmt;
using namespace trans;

namespace {

class RootBlockObjCVarRewriter :
                          public RecursiveASTVisitor<RootBlockObjCVarRewriter> {
  MigrationPass &Pass;
  llvm::DenseSet<VarDecl *> &VarsToChange;

  class BlockVarChecker : public RecursiveASTVisitor<BlockVarChecker> {
    VarDecl *Var;
  
    typedef RecursiveASTVisitor<BlockVarChecker> base;
  public:
    BlockVarChecker(VarDecl *var) : Var(var) { }
  
    bool TraverseImplicitCastExpr(ImplicitCastExpr *castE) {
      if (DeclRefExpr *
            ref = dyn_cast<DeclRefExpr>(castE->getSubExpr())) {
        if (ref->getDecl() == Var) {
          if (castE->getCastKind() == CK_LValueToRValue)
            return true; // Using the value of the variable.
          if (castE->getCastKind() == CK_NoOp && castE->isLValue() &&
              Var->getASTContext().getLangOpts().CPlusPlus)
            return true; // Binding to const C++ reference.
        }
      }

      return base::TraverseImplicitCastExpr(castE);
    }

    bool VisitDeclRefExpr(DeclRefExpr *E) {
      if (E->getDecl() == Var)
        return false; // The reference of the variable, and not just its value,
                      //  is needed.
      return true;
    }
  };

public:
  RootBlockObjCVarRewriter(MigrationPass &pass,
                           llvm::DenseSet<VarDecl *> &VarsToChange)
    : Pass(pass), VarsToChange(VarsToChange) { }

  bool VisitBlockDecl(BlockDecl *block) {
    SmallVector<VarDecl *, 4> BlockVars;
    
    for (BlockDecl::capture_iterator
           I = block->capture_begin(), E = block->capture_end(); I != E; ++I) {
      VarDecl *var = I->getVariable();
      if (I->isByRef() &&
          var->getType()->isObjCObjectPointerType() &&
          isImplicitStrong(var->getType())) {
        BlockVars.push_back(var);
      }
    }

    for (unsigned i = 0, e = BlockVars.size(); i != e; ++i) {
      VarDecl *var = BlockVars[i];

      BlockVarChecker checker(var);
      bool onlyValueOfVarIsNeeded = checker.TraverseStmt(block->getBody());
      if (onlyValueOfVarIsNeeded)
        VarsToChange.insert(var);
      else
        VarsToChange.erase(var);
    }

    return true;
  }

private:
  bool isImplicitStrong(QualType ty) {
    if (isa<AttributedType>(ty.getTypePtr()))
      return false;
    return ty.getLocalQualifiers().getObjCLifetime() == Qualifiers::OCL_Strong;
  }
};

class BlockObjCVarRewriter : public RecursiveASTVisitor<BlockObjCVarRewriter> {
  MigrationPass &Pass;
  llvm::DenseSet<VarDecl *> &VarsToChange;

public:
  BlockObjCVarRewriter(MigrationPass &pass,
                       llvm::DenseSet<VarDecl *> &VarsToChange)
    : Pass(pass), VarsToChange(VarsToChange) { }

  bool TraverseBlockDecl(BlockDecl *block) {
    RootBlockObjCVarRewriter(Pass, VarsToChange).TraverseDecl(block);
    return true;
  }
};

} // anonymous namespace

void BlockObjCVariableTraverser::traverseBody(BodyContext &BodyCtx) {
  MigrationPass &Pass = BodyCtx.getMigrationContext().Pass;
  llvm::DenseSet<VarDecl *> VarsToChange;

  BlockObjCVarRewriter trans(Pass, VarsToChange);
  trans.TraverseStmt(BodyCtx.getTopStmt());

  for (llvm::DenseSet<VarDecl *>::iterator
         I = VarsToChange.begin(), E = VarsToChange.end(); I != E; ++I) {
    VarDecl *var = *I;
    BlocksAttr *attr = var->getAttr<BlocksAttr>();
    if(!attr)
      continue;
    bool useWeak = canApplyWeak(Pass.Ctx, var->getType());
    SourceManager &SM = Pass.Ctx.getSourceManager();
    Transaction Trans(Pass.TA);
    Pass.TA.replaceText(SM.getExpansionLoc(attr->getLocation()),
                        "__block",
                        useWeak ? "__weak" : "__unsafe_unretained");
  }
}
