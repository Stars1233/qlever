// Copyright 2022 - 2024, University of Freiburg
// Chair of Algorithms and Data Structures
// Author: Johannes Kalmbach <kalmbacj@cs.uni-freiburg.de>

#include "util/Conversions.h"

#include <absl/strings/str_cat.h>

#include <ctre-unicode.hpp>

#include "global/Constants.h"
#include "parser/NormalizedString.h"

namespace ad_utility {

// _________________________________________________________
triple_component::Iri convertLangtagToEntityUri(const std::string& tag) {
  return triple_component::Iri::fromIriref(makeQleverInternalIri("@", tag));
}

// _________________________________________________________
triple_component::Iri convertToLanguageTaggedPredicate(
    const triple_component::Iri& pred, const std::string& langtag) {
  return triple_component::Iri::fromIriref(absl::StrCat(
      "@", langtag, "@<", asStringViewUnsafe(pred.getContent()), ">"));
}

}  // namespace ad_utility
