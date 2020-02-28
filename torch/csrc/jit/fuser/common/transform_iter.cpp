#include <torch/csrc/jit/fuser/common/fusion.h>
#include <torch/csrc/jit/fuser/common/transform_iter.h>

namespace torch {
namespace jit {
namespace fuser {

void TransformIter::replayBackward(Split* expr) {}

void TransformIter::replayBackward(Merge* expr) {}

void TransformIter::replayBackward(Reorder* expr) {}

void TransformIter::replayBackward(Expr* expr) {
  TORCH_CHECK(expr->isExpr());
  switch (*(expr->getExprType())) {
    case (ExprType::Split):
      replayBackward(static_cast<Split*>(expr));
      break;
    case (ExprType::Merge):
      replayBackward(static_cast<Merge*>(expr));
      break;
    case (ExprType::Reorder):
      replayBackward(static_cast<Reorder*>(expr));
      break;
    default:
      throw std::runtime_error("Could not detect expr type in replayBackward.");
  }
}

TensorDomain* TransformIter::runBackward(
    TensorDomain* td,
    bool generate_record) {
  if (generate_record)
    record = std::vector<Expr*>();

  TensorDomain* root = td; // backward running td
  Fusion* fusion = FusionGuard::getCurFusion();

  // Get my origin
  Expr* orig = fusion->origin(root);
  std::set<Expr*> visited_exprs;

  // If I'm not back to the original td
  while (orig != nullptr) {
    if (visited_exprs.find(orig) != visited_exprs.end())
      throw std::runtime_error(
          "TransformReplay::get_root is not traversing a correct history.");

    visited_exprs.emplace(orig);
    TensorDomain* previous_td = nullptr;
    // Check inputs of this operation, make sure there isn't more than one TD
    // I can only record operations that only take this TD as an input.
    for (Val* inp : orig->inputs())
      if (inp->getValType() == ValType::TensorDomain) {
        if (previous_td != nullptr)
          throw std::runtime_error(
              "TransformReplay::get_root could not decifer transform history of a TensorDomain.");

        // Place transform op on top of stack.
        if (generate_record)
          record.push_back(orig);

        // Traverse back
        root = static_cast<TensorDomain*>(inp);
        orig = fusion->origin(root);
      }
  }
  if (generate_record)
    std::reverse(record.begin(), record.end());

  return root;
}

TensorView* TransformIter::replay(Split* expr, TensorView* tv) {
  split(tv, expr->axis(), static_cast<Int*>(expr->factor())->value().value());
}

TensorView* TransformIter::replay(Merge* expr, TensorView* tv) {
  return merge(tv, expr->axis());
}

TensorView* TransformIter::replay(Reorder* expr, TensorView* tv) {
  std::unordered_map<int, int> axis2pos;
  for(decltype(expr->pos2axis().size()) i{0}; i<expr->pos2axis().size(); i++)
    axis2pos[expr->pos2axis()[i]] = i;
  return reorder(tv, axis2pos);
}

TensorView* TransformIter::replay(Expr* expr, TensorView* tv) {
  TORCH_CHECK(expr->isExpr());
  switch (*(expr->getExprType())) {
    case (ExprType::Split):
      return replay(static_cast<Split*>(expr), tv);
    case (ExprType::Merge):
      return replay(static_cast<Merge*>(expr), tv);
    case (ExprType::Reorder):
      return replay(static_cast<Reorder*>(expr), tv);
    default:
      throw std::runtime_error("Could not detect expr type in replay.");
  }
}

TensorView* TransformIter::runReplay(TensorView* tv) {
  for (auto it = record.begin(); it < record.end(); ++it) {
    tv = TransformIter::replay(*it, tv);
  }
  return tv;
}

} // namespace fuser
} // namespace jit
} // namespace torch
