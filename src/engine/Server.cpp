// Copyright 2011, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Author: Björn Buchhold <buchholb>

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

#include "../util/StringUtils.h"
#include "../util/Log.h"
#include "../parser/ParseException.h"
#include "./Server.h"


// _____________________________________________________________________________
void Server::initialize(const string& ontologyBaseName) {
  LOG(INFO) << "Initializing server..." << std::endl;

  // Init the index.
  _index.createFromOnDiskIndex(ontologyBaseName);

  // Init the server socket.
  bool ret = _serverSocket.create() && _serverSocket.bind(_port)
      && _serverSocket.listen();
  if (!ret) {
    LOG(ERROR)
      << "Failed to create socket on port " << _port << "." << std::endl;
    exit(1);
  }

  // Set flag.
  _initialized = true;
  LOG(INFO) << "Done initializing server." << std::endl;
}

// _____________________________________________________________________________
void Server::run() {
  if (!_initialized) {
    LOG(ERROR) << "Cannot start an uninitialized server!" << std::endl;
    exit(1);
  }
  // For now, only use one QueryExecutionContext at all time.
  // This may be changed for some implementations of multi threading.
  // Cache(s) are associated with this execution context.
  QueryExecutionContext qec(_index, _engine);

  // Loop and wait for queries. Run forever, for now.
  while (true) {
    // Wait for new query
    LOG(INFO)
      << "---------- WAITING FOR QUERY AT PORT \"" << _port << "\" ... "
          << std::endl;

    ad_utility::Socket client;
    bool success = _serverSocket.acceptClient(&client);
    if (!success) {
      LOG(ERROR) << "Socket error while trying to accept client" << std::endl;
      continue;
    }
    // TODO(buchholb): Spawn a new thread here / use a ThreadPool / etc.
    LOG(INFO) << "Incoming connection, processing..." << std::endl;
    process(&client, &qec);
  }
}

// _____________________________________________________________________________
void Server::process(Socket* client, QueryExecutionContext* qec) const {
  string request;
  string response;
  string query;
  client->recieve(&request);
  try {
    ParamValueMap params = parseHttpRequest(request);
    if (ad_utility::getLowercase(params["cmd"]) == "clearcache") {
      qec->clearCache();
    }
    query = createQueryFromHttpParams(params);
    LOG(INFO) << "Query: " << query << '\n';
    ParsedQuery pq = SparqlParser::parse(query);
    pq.expandPrefixes();
    QueryGraph qg(qec);
    qg.createFromParsedQuery(pq);
    const QueryExecutionTree& qet = qg.getExecutionTree();
    response = composeResponseJson(pq, qet);
  } catch (const ad_semsearch::Exception& e) {
    response = composeResponseJson(query, e);
  } catch (const ParseException& e) {
      response = composeResponseJson(query, e);
  }

  string contentType = "application/json";
  string httpResponse = createHttpResponse(response, contentType);
  client->send(httpResponse);
}

// _____________________________________________________________________________
Server::ParamValueMap Server::parseHttpRequest(
    const string& httpRequest) const {
  LOG(DEBUG) << "Parsing HTTP Request." << endl;
  ParamValueMap params;
  _requestProcessingTimer.start();
  // Parse the HTTP Request.

  size_t indexOfGET = httpRequest.find("GET");
  size_t indexOfHTTP = httpRequest.find("HTTP");

  if (indexOfGET == httpRequest.npos || indexOfHTTP == httpRequest.npos) {
    AD_THROW(ad_semsearch::Exception::BAD_REQUEST, "Invalid request. "
        "Only supporting proper HTTP GET requests!");
  }

  string request = httpRequest.substr(indexOfGET + 3,
      indexOfHTTP - (indexOfGET + 3));

  size_t index = request.find("?");
  if (index == request.npos) {
    AD_THROW(ad_semsearch::Exception::BAD_REQUEST, "Invalid request. "
        "At least one parameters is required for meaningful queries!");
  }
  size_t next = request.find('&', index + 1);
  while (next != request.npos) {
    size_t posOfEq = request.find('=', index + 1);
    if (posOfEq == request.npos) {
      AD_THROW(ad_semsearch::Exception::BAD_REQUEST,
          "Parameter without \"=\" in HTTP Request.");
    }
    string param = ad_utility::getLowercaseUtf8(
        request.substr(index + 1, posOfEq - (index + 1)));
    string value = ad_utility::decodeUrl(
        request.substr(posOfEq + 1, next - (posOfEq + 1)));
    if (params.count(param) > 0) {
      AD_THROW(ad_semsearch::Exception::BAD_REQUEST,
          "Duplicate HTTP parameter: " + param);
    }
    params[param] = value;
    index = next;
    next = request.find('&', index + 1);
  }
  size_t posOfEq = request.find('=', index + 1);
  if (posOfEq == request.npos) {
    AD_THROW(ad_semsearch::Exception::BAD_REQUEST,
        "Parameter without \"=\" in HTTP Request.");
  }
  string param = ad_utility::getLowercaseUtf8(
      request.substr(index + 1, posOfEq - (index + 1)));
  string value = ad_utility::decodeUrl(request.substr(posOfEq + 1,
      request.size() - 1 - (posOfEq + 1)));
  if (params.count(param) > 0) {
    AD_THROW(ad_semsearch::Exception::BAD_REQUEST, "Duplicate HTTP parameter.");
  }
  params[param] = value;

  LOG(DEBUG) << "Done parsing HTTP Request." << endl;
  return params;
}

// _____________________________________________________________________________
string Server::createQueryFromHttpParams(const ParamValueMap& params) const {
  string query;
  // Construct a Query object from the parsed request.
  auto it = params.find("query");
  if (it == params.end() || it->second == "") {
    AD_THROW(ad_semsearch::Exception::BAD_REQUEST,
        "Expected at least one non-empty attribute \"query\".");
  }
  return it->second;
}

// _____________________________________________________________________________
string Server::createHttpResponse(const string& content,
    const string& contentType) const {
  std::ostringstream os;
  os << "HTTP/1.0 200 OK\r\n" << "Content-Length: " << content.size() << "\r\n"
      << "Connection: close\r\n" << "Content-Type: " << contentType
      << "; charset=" << "UTF-8" << "\r\n" << "\r\n" << content;
  return os.str();
}

// _____________________________________________________________________________
string Server::composeResponseJson(const ParsedQuery& query,
    const QueryExecutionTree& qet) const {

  ResultTable rt = qet.getResult();
  _requestProcessingTimer.stop();
  off_t compResultMsecs = _requestProcessingTimer.msecs();
  size_t resultSize = rt.size();


  std::ostringstream os;
  os << "{\r\n"
      << "\"result\": {\r\n"
      << "\"query\": \""
      << ad_utility::escapeForJson(query._originalString)
      << "\",\r\n"
      << "\"status\": \"OK\",\r\n"
      << "\"result-size\": \"" << resultSize << "\",\r\n";

  os << "\"res\": \"";
  size_t limit = MAX_NOF_ROWS_IN_RESULT;
  size_t offset = 0;
  if (query._limit.size() > 0) {
    limit = static_cast<size_t>(atol(query._limit.c_str()));
  }
  if (query._offset.size() > 0) {
    offset = static_cast<size_t>(atol(query._offset.c_str()));
  }
  _requestProcessingTimer.cont();
  qet.writeResultToStream(os, query._selectedVariables, limit, offset);
  _requestProcessingTimer.stop();
  os << "\",\r\n";

  os << "\"time\": {\r\n"
      << "\"total\": \"" << _requestProcessingTimer.msecs()
      << "ms\",\r\n"
      << "\"computeResult\": \"" << compResultMsecs
      << "ms\"\r\n"
      << "}\r\n"
      << "}\r\n"
      << "}\r\n";

  return os.str();
}

// _____________________________________________________________________________
string Server::composeResponseJson(const string& query,
    const ad_semsearch::Exception& exception) const {
  std::ostringstream os;
  _requestProcessingTimer.stop();

  os << "{\r\n"
      << "\"result\": {\r\n"
      << "\"query\": \""
      << ad_utility::escapeForJson(query)
      << "\",\r\n"
      << "\"status\": \"ERROR\",\r\n"
      << "\"result-size\": \"0\",\r\n"
      << "\"time\": {\r\n"
      << "\"total\": \"" << _requestProcessingTimer.msecs()
      << "ms\",\r\n"
      << "\"computeResult\": \"" << _requestProcessingTimer.msecs()
      << "ms\"\r\n"
      << "},\r\n";


  string msg = ad_utility::escapeForJson(exception.getFullErrorMessage());

  os << "\"Exception-Error-Message\": \"" << msg << "\"\r\n"
      << "}\r\n"
      << "}\r\n";

  return os.str();
}

// _____________________________________________________________________________
string Server::composeResponseJson(const string& query,
    const ParseException& exception) const {
  std::ostringstream os;
  _requestProcessingTimer.stop();

  os << "{\r\n"
      << "\"result\": {\r\n"
      << "\"query\": \""
      << ad_utility::escapeForJson(query)
      << "\",\r\n"
      << "\"status\": \"ERROR\",\r\n"
      << "\"result-size\": \"0\",\r\n"
      << "\"time\": {\r\n"
      << "\"total\": \"" << _requestProcessingTimer.msecs()
      << "ms\",\r\n"
      << "\"computeResult\": \"" << _requestProcessingTimer.msecs()
      << "ms\"\r\n"
      << "},\r\n";


  string msg = ad_utility::escapeForJson(exception.what());

  os << "\"Exception-Error-Message\": \"" << msg << "\"\r\n"
      << "}\r\n"
      << "}\r\n";

  return os.str();
}