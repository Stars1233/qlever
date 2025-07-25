// Copyright 2025, University of Freiburg
// Chair of Algorithms and Data Structures
// Authors: Julian Mundhahs <mundhahj@tf.uni-freiburg.de>

#ifndef QLEVER_SRC_ENGINE_SPARQLPROTOCOL_H
#define QLEVER_SRC_ENGINE_SPARQLPROTOCOL_H

#include "engine/ParsedRequestBuilder.h"

// Parses HTTP requests to `ParsedRequests` (a representation of Query, Update,
// Graph Store and internal operations) according to the SPARQL specifications.
class SparqlProtocol {
  FRIEND_TEST(SparqlProtocolTest, parseGET);
  FRIEND_TEST(SparqlProtocolTest, parseUrlencodedPOST);
  FRIEND_TEST(SparqlProtocolTest, parseQueryPOST);
  FRIEND_TEST(SparqlProtocolTest, parseUpdatePOST);
  FRIEND_TEST(SparqlProtocolTest, parsePOST);
  FRIEND_TEST(SparqlProtocolTest, parseGraphStoreProtocolIndirect);
  FRIEND_TEST(SparqlProtocolTest, parseGraphStoreProtocolDirect);

  static constexpr std::string_view contentTypeUrlEncoded =
      "application/x-www-form-urlencoded";
  static constexpr std::string_view contentTypeSparqlQuery =
      "application/sparql-query";
  static constexpr std::string_view contentTypeSparqlUpdate =
      "application/sparql-update";

  using RequestType = ParsedRequestBuilder::RequestType;

  // Parse an HTTP GET request into a `ParsedRequest`. The
  // `ParsedRequestBuilder` must have already extracted the access token.
  static ad_utility::url_parser::ParsedRequest parseGET(
      const RequestType& request);

  // Parse an HTTP POST request with content-type
  // `application/x-www-form-urlencoded` into a `ParsedRequest`.
  static ad_utility::url_parser::ParsedRequest parseUrlencodedPOST(
      const RequestType& request);

  // Parse an HTTP POST request with a SPARQL operation in its body
  // into a `ParsedRequest`. This is used for the content types
  // `application/sparql-query` and `application/sparql-update`.
  template <typename Operation>
  static ad_utility::url_parser::ParsedRequest parseSPARQLPOST(
      const RequestType& request, std::string_view contentType);

  // Parse an HTTP POST request into a `ParsedRequest`.
  static ad_utility::url_parser::ParsedRequest parsePOST(
      const RequestType& request);

  // Parse a Graph Store Protocol request with direct or indirect graph
  // identification.
  static ad_utility::url_parser::ParsedRequest parseGraphStoreProtocolIndirect(
      const RequestType& request);
  static ad_utility::url_parser::ParsedRequest parseGraphStoreProtocolDirect(
      const RequestType& request);

 public:
  // Parse a HTTP request.
  static ad_utility::url_parser::ParsedRequest parseHttpRequest(
      RequestType& request);
};

#endif  // QLEVER_SRC_ENGINE_SPARQLPROTOCOL_H
