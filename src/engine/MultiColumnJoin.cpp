// Copyright 2018 - 2025, University of Freiburg
// Chair of Algorithms and Data Structures
// Authors: Florian Kramer [2018 - 2020]
//          Johannes Kalmbach <kalmbach@cs.uni-freiburg.de>

#include "engine/MultiColumnJoin.h"

#include "engine/AddCombinedRowToTable.h"
#include "engine/CallFixedSize.h"
#include "engine/Engine.h"
#include "engine/JoinHelpers.h"
#include "util/JoinAlgorithms/JoinAlgorithms.h"

using std::endl;
using std::string;

// _____________________________________________________________________________
MultiColumnJoin::MultiColumnJoin(QueryExecutionContext* qec,
                                 std::shared_ptr<QueryExecutionTree> t1,
                                 std::shared_ptr<QueryExecutionTree> t2,
                                 bool allowSwappingChildrenOnlyForTesting)
    : Operation{qec} {
  // Make sure subtrees are ordered so that identical queries can be identified.
  if (allowSwappingChildrenOnlyForTesting &&
      t1->getCacheKey() > t2->getCacheKey()) {
    std::swap(t1, t2);
  }
  std::tie(_left, _right, _joinColumns) =
      QueryExecutionTree::getSortedSubtreesAndJoinColumns(std::move(t1),
                                                          std::move(t2));
}

// _____________________________________________________________________________
string MultiColumnJoin::getCacheKeyImpl() const {
  std::ostringstream os;
  os << "MULTI_COLUMN_JOIN\n" << _left->getCacheKey() << " ";
  os << "join-columns: [";
  for (size_t i = 0; i < _joinColumns.size(); i++) {
    os << _joinColumns[i][0] << (i < _joinColumns.size() - 1 ? " & " : "");
  };
  os << "]\n";
  os << "|X|\n" << _right->getCacheKey() << " ";
  os << "join-columns: [";
  for (size_t i = 0; i < _joinColumns.size(); i++) {
    os << _joinColumns[i][1] << (i < _joinColumns.size() - 1 ? " & " : "");
  };
  os << "]";
  return std::move(os).str();
}

// _____________________________________________________________________________
string MultiColumnJoin::getDescriptor() const {
  std::string joinVars = "";
  for (auto jc : _joinColumns) {
    joinVars +=
        _left->getVariableAndInfoByColumnIndex(jc[0]).first.name() + " ";
  }
  return "MultiColumnJoin on " + joinVars;
}

// _____________________________________________________________________________
Result MultiColumnJoin::computeResult([[maybe_unused]] bool requestLaziness) {
  LOG(DEBUG) << "MultiColumnJoin result computation..." << endl;

  IdTable idTable{getExecutionContext()->getAllocator()};
  idTable.setNumColumns(getResultWidth());

  AD_CONTRACT_CHECK(idTable.numColumns() >= _joinColumns.size());

  const auto leftResult = _left->getResult();
  const auto rightResult = _right->getResult();

  checkCancellation();

  LOG(DEBUG) << "MultiColumnJoin subresult computation done." << std::endl;

  LOG(DEBUG) << "Computing a multi column join between results of size "
             << leftResult->idTable().size() << " and "
             << rightResult->idTable().size() << endl;

  computeMultiColumnJoin(leftResult->idTable(), rightResult->idTable(),
                         _joinColumns, &idTable);

  checkCancellation();

  LOG(DEBUG) << "MultiColumnJoin result computation done" << endl;
  // If only one of the two operands has a non-empty local vocabulary, share
  // with that one (otherwise, throws an exception).
  return {std::move(idTable), resultSortedOn(),
          Result::getMergedLocalVocab(*leftResult, *rightResult)};
}

// _____________________________________________________________________________
VariableToColumnMap MultiColumnJoin::computeVariableToColumnMap() const {
  return makeVarToColMapForJoinOperation(
      _left->getVariableColumns(), _right->getVariableColumns(), _joinColumns,
      BinOpType::Join, _left->getResultWidth());
}

// _____________________________________________________________________________
size_t MultiColumnJoin::getResultWidth() const {
  size_t res =
      _left->getResultWidth() + _right->getResultWidth() - _joinColumns.size();
  AD_CONTRACT_CHECK(res > 0);
  return res;
}

// _____________________________________________________________________________
std::vector<ColumnIndex> MultiColumnJoin::resultSortedOn() const {
  std::vector<ColumnIndex> sortedOn;
  // The result is sorted on all join columns from the left subtree.
  for (const auto& a : _joinColumns) {
    sortedOn.push_back(a[0]);
  }
  return sortedOn;
}

// _____________________________________________________________________________
float MultiColumnJoin::getMultiplicity(size_t col) {
  if (!_multiplicitiesComputed) {
    computeSizeEstimateAndMultiplicities();
  }
  return _multiplicities[col];
}

// _____________________________________________________________________________
uint64_t MultiColumnJoin::getSizeEstimateBeforeLimit() {
  if (!_multiplicitiesComputed) {
    computeSizeEstimateAndMultiplicities();
  }
  return _sizeEstimate;
}

// _____________________________________________________________________________
size_t MultiColumnJoin::getCostEstimate() {
  size_t costEstimate = getSizeEstimateBeforeLimit() +
                        _left->getSizeEstimate() + _right->getSizeEstimate();
  // This join is slower than a normal join, due to
  // its increased complexity
  costEstimate *= 2;
  // Make the join 7% more expensive per join column
  costEstimate *= (1 + (_joinColumns.size() - 1) * 0.07);
  return _left->getCostEstimate() + _right->getCostEstimate() + costEstimate;
}

// _____________________________________________________________________________
void MultiColumnJoin::computeSizeEstimateAndMultiplicities() {
  // The number of distinct entries in the result is at most the minimum of
  // the numbers of distinct entries in all join columns.
  // The multiplicity in the result is approximated by the product of the
  // maximum of the multiplicities of each side.

  // compute the minimum number of distinct elements in the join columns
  size_t numDistinctLeft = std::numeric_limits<size_t>::max();
  size_t numDistinctRight = std::numeric_limits<size_t>::max();
  for (size_t i = 0; i < _joinColumns.size(); i++) {
    size_t dl = std::max(1.0f, _left->getSizeEstimate() /
                                   _left->getMultiplicity(_joinColumns[i][0]));
    size_t dr = std::max(1.0f, _right->getSizeEstimate() /
                                   _right->getMultiplicity(_joinColumns[i][1]));
    numDistinctLeft = std::min(numDistinctLeft, dl);
    numDistinctRight = std::min(numDistinctRight, dr);
  }
  size_t numDistinctResult = std::min(numDistinctLeft, numDistinctRight);

  // compute an estimate for the results multiplicity
  float multLeft = std::numeric_limits<float>::max();
  float multRight = std::numeric_limits<float>::max();
  for (size_t i = 0; i < _joinColumns.size(); i++) {
    multLeft = std::min(multLeft, _left->getMultiplicity(_joinColumns[i][0]));
    multRight =
        std::min(multRight, _right->getMultiplicity(_joinColumns[i][1]));
  }
  float multResult = multLeft * multRight;

  _sizeEstimate = multResult * numDistinctResult;
  // Don't estimate 0 since then some parent operations
  // (in particular joins) using isKnownEmpty() will
  // will assume the size to be exactly zero
  _sizeEstimate += 1;

  // compute estimates for the multiplicities of the result columns
  _multiplicities.clear();

  for (size_t i = 0; i < _left->getResultWidth(); i++) {
    float mult = _left->getMultiplicity(i) * (multResult / multLeft);
    _multiplicities.push_back(mult);
  }

  for (size_t i = 0; i < _right->getResultWidth(); i++) {
    bool isJcl = false;
    for (size_t j = 0; j < _joinColumns.size(); j++) {
      if (_joinColumns[j][1] == i) {
        isJcl = true;
        break;
      }
    }
    if (isJcl) {
      continue;
    }
    float mult = _right->getMultiplicity(i) * (multResult / multRight);
    _multiplicities.push_back(mult);
  }
  _multiplicitiesComputed = true;
}

// _______________________________________________________________________
void MultiColumnJoin::computeMultiColumnJoin(
    const IdTable& left, const IdTable& right,
    const std::vector<std::array<ColumnIndex, 2>>& joinColumns,
    IdTable* result) {
  // check for trivial cases
  if (left.empty() || right.empty()) {
    return;
  }

  ad_utility::JoinColumnMapping joinColumnData{joinColumns, left.numColumns(),
                                               right.numColumns()};

  IdTableView<0> leftJoinColumns =
      left.asColumnSubsetView(joinColumnData.jcsLeft());
  IdTableView<0> rightJoinColumns =
      right.asColumnSubsetView(joinColumnData.jcsRight());

  auto leftPermuted = left.asColumnSubsetView(joinColumnData.permutationLeft());
  auto rightPermuted =
      right.asColumnSubsetView(joinColumnData.permutationRight());

  auto rowAdder = ad_utility::AddCombinedRowToIdTable(
      joinColumns.size(), leftPermuted, rightPermuted, std::move(*result),
      cancellationHandle_);
  auto addRow = [&rowAdder, beginLeft = leftJoinColumns.begin(),
                 beginRight = rightJoinColumns.begin()](const auto& itLeft,
                                                        const auto& itRight) {
    rowAdder.addRow(itLeft - beginLeft, itRight - beginRight);
  };

  // Compute `isCheap`, which is true iff there are no UNDEF values in the join
  // columns (in which case we can use a simpler and cheaper join algorithm).
  //
  // TODO<joka921> This is the most common case. There are many other cases
  // where the generic `zipperJoinWithUndef` can be optimized. We will those
  // for a later PR.
  bool isCheap = ql::ranges::none_of(joinColumns, [&](const auto& jcs) {
    auto [leftCol, rightCol] = jcs;
    return (ql::ranges::any_of(right.getColumn(rightCol), &Id::isUndefined)) ||
           (ql::ranges::any_of(left.getColumn(leftCol), &Id::isUndefined));
  });

  auto checkCancellationLambda = [this] { checkCancellation(); };

  const size_t numOutOfOrder = [&]() {
    if (isCheap) {
      return ad_utility::zipperJoinWithUndef(
          leftJoinColumns, rightJoinColumns,
          ql::ranges::lexicographical_compare, addRow, ad_utility::noop,
          ad_utility::noop, ad_utility::noop, checkCancellationLambda);
    } else {
      return ad_utility::zipperJoinWithUndef(
          leftJoinColumns, rightJoinColumns,
          ql::ranges::lexicographical_compare, addRow,
          ad_utility::findSmallerUndefRanges,
          ad_utility::findSmallerUndefRanges, ad_utility::noop,
          checkCancellationLambda);
    }
  }();
  *result = std::move(rowAdder).resultTable();
  // If there were UNDEF values in the input, the result might be out of
  // order. Sort it, because this operation promises a sorted result in its
  // `resultSortedOn()` member function.
  // TODO<joka921> We only have to do this if the sorting is required (merge the
  // other PR first).
  if (numOutOfOrder > 0) {
    std::vector<ColumnIndex> cols;
    for (size_t i = 0; i < joinColumns.size(); ++i) {
      cols.push_back(i);
    }
    checkCancellation();
    Engine::sort(*result, cols);
  }

  // The result that `zipperJoinWithUndef` produces has a different order of
  // columns than expected, permute them. See the documentation of
  // `JoinColumnMapping` for details.
  result->setColumnSubset(joinColumnData.permutationResult());
  checkCancellation();
}

// _____________________________________________________________________________
std::unique_ptr<Operation> MultiColumnJoin::cloneImpl() const {
  auto copy = std::make_unique<MultiColumnJoin>(*this);
  copy->_left = _left->clone();
  copy->_right = _right->clone();
  return copy;
}

// _____________________________________________________________________________
bool MultiColumnJoin::columnOriginatesFromGraphOrUndef(
    const Variable& variable) const {
  AD_CONTRACT_CHECK(getExternallyVisibleVariableColumns().contains(variable));
  // For the join columns we don't union the elements, we intersect them so we
  // can have a more efficient implementation.
  if (_left->getVariableColumnOrNullopt(variable).has_value() &&
      _right->getVariableColumnOrNullopt(variable).has_value()) {
    using namespace qlever::joinHelpers;
    return doesJoinProduceGuaranteedGraphValuesOrUndef(_left, _right, variable);
  }
  return Operation::columnOriginatesFromGraphOrUndef(variable);
}
