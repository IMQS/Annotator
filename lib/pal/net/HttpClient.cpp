#include "pch.h"
#include "HttpClient.h"
#include "url.h"
#include "../strings/strings.h"
#include "../modp/modp_b64.h"

namespace imqs {
namespace http {

static std::string DefaultCACertFile;

static StaticError ErrResolveProxyFailed("ResolveProxy Failed");
static StaticError ErrResolveHostFailed("ResolveHost Failed");
static StaticError ErrConnectionFailed("Connection Failed");
static StaticError ErrTimeout("Timeout");
static StaticError ErrTooManyRedirects("Too Many Redirects");
static StaticError ErrNoHeadersInResponse("No Headers In Response");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

IMQS_PAL_API const char* StatusAndMessage(Status code) {
	switch (code) {
	case Status::Status100_Continue: return "100 Continue";
	case Status::Status101_Switching_Protocols: return "101 Switching Protocols";
	case Status::Status102_Processing: return "102 Processing";
	case Status::Status200_OK: return "200 OK";
	case Status::Status201_Created: return "201 Created";
	case Status::Status202_Accepted: return "202 Accepted";
	case Status::Status203_Non_Authoritative_Information: return "203 Non Authoritative Information";
	case Status::Status204_No_Content: return "204 No Content";
	case Status::Status205_Reset_Content: return "205 Reset Content";
	case Status::Status206_Partial_Content: return "206 Partial Content";
	case Status::Status207_Multi_Status: return "207 Multi Status";
	case Status::Status208_Already_Reported: return "208 Already Reported";
	case Status::Status226_IM_Used: return "226 IM Used";
	case Status::Status300_Multiple_Choices: return "300 Multiple Choices";
	case Status::Status301_Moved_Permanently: return "301 Moved Permanently";
	case Status::Status302_Found: return "302 Found";
	case Status::Status303_See_Other: return "303 See Other";
	case Status::Status304_Not_Modified: return "304 Not Modified";
	case Status::Status305_Use_Proxy: return "305 Use Proxy";
	case Status::Status307_Temporary_Redirect: return "307 Temporary Redirect";
	case Status::Status308_Permanent_Redirect: return "308 Permanent Redirect";
	case Status::Status400_Bad_Request: return "400 Bad Request";
	case Status::Status401_Unauthorized: return "401 Unauthorized";
	case Status::Status402_Payment_Required: return "402 Payment Required";
	case Status::Status403_Forbidden: return "403 Forbidden";
	case Status::Status404_Not_Found: return "404 Not Found";
	case Status::Status405_Method_Not_Allowed: return "405 Method Not Allowed";
	case Status::Status406_Not_Acceptable: return "406 Not Acceptable";
	case Status::Status407_Proxy_Authentication_Required: return "407 Proxy Authentication Required";
	case Status::Status408_Request_Timeout: return "408 Request Timeout";
	case Status::Status409_Conflict: return "409 Conflict";
	case Status::Status410_Gone: return "410 Gone";
	case Status::Status411_Length_Required: return "411 Length Required";
	case Status::Status412_Precondition_Failed: return "412 Precondition Failed";
	case Status::Status413_Payload_Too_Large: return "413 Payload Too Large";
	case Status::Status414_URI_Too_Long: return "414 URI Too Long";
	case Status::Status415_Unsupported_Media_Type: return "415 Unsupported Media Type";
	case Status::Status416_Range_Not_Satisfiable: return "416 Range Not Satisfiable";
	case Status::Status417_Expectation_Failed: return "417 Expectation Failed";
	case Status::Status421_Misdirected_Request: return "421 Misdirected Request";
	case Status::Status422_Unprocessable_Entity: return "422 Unprocessable Entity";
	case Status::Status423_Locked: return "423 Locked";
	case Status::Status424_Failed_Dependency: return "424 Failed Dependency";
	case Status::Status425_Unassigned: return "425 Unassigned";
	case Status::Status426_Upgrade_Required: return "426 Upgrade Required";
	case Status::Status427_Unassigned: return "427 Unassigned";
	case Status::Status428_Precondition_Required: return "428 Precondition Required";
	case Status::Status429_Too_Many_Requests: return "429 Too Many Requests";
	case Status::Status430_Unassigned: return "430 Unassigned";
	case Status::Status431_Request_Header_Fields_Too_Large: return "431 Request Header Fields Too Large";
	case Status::Status500_Internal_Server_Error: return "500 Internal Server Error";
	case Status::Status501_Not_Implemented: return "501 Not Implemented";
	case Status::Status502_Bad_Gateway: return "502 Bad Gateway";
	case Status::Status503_Service_Unavailable: return "503 Service Unavailable";
	case Status::Status504_Gateway_Timeout: return "504 Gateway Timeout";
	case Status::Status505_HTTP_Version_Not_Supported: return "505 HTTP Version Not Supported";
	case Status::Status506_Variant_Also_Negotiates: return "506 Variant Also Negotiates";
	case Status::Status507_Insufficient_Storage: return "507 Insufficient Storage";
	case Status::Status508_Loop_Detected: return "508 Loop Detected";
	case Status::Status509_Unassigned: return "509 Unassigned";
	case Status::Status510_Not_Extended: return "510 Not Extended";
	case Status::Status511_Network_Authentication_Required: return "511 Network Authentication Required";
	default: return "000 Unknown";
	}
}

IMQS_PAL_API bool IsValidStatus(int code) {
	switch (code) {
	case (int) Status::Status100_Continue:
	case (int) Status::Status101_Switching_Protocols:
	case (int) Status::Status102_Processing:
	case (int) Status::Status200_OK:
	case (int) Status::Status201_Created:
	case (int) Status::Status202_Accepted:
	case (int) Status::Status203_Non_Authoritative_Information:
	case (int) Status::Status204_No_Content:
	case (int) Status::Status205_Reset_Content:
	case (int) Status::Status206_Partial_Content:
	case (int) Status::Status207_Multi_Status:
	case (int) Status::Status208_Already_Reported:
	case (int) Status::Status226_IM_Used:
	case (int) Status::Status300_Multiple_Choices:
	case (int) Status::Status301_Moved_Permanently:
	case (int) Status::Status302_Found:
	case (int) Status::Status303_See_Other:
	case (int) Status::Status304_Not_Modified:
	case (int) Status::Status305_Use_Proxy:
	case (int) Status::Status307_Temporary_Redirect:
	case (int) Status::Status308_Permanent_Redirect:
	case (int) Status::Status400_Bad_Request:
	case (int) Status::Status401_Unauthorized:
	case (int) Status::Status402_Payment_Required:
	case (int) Status::Status403_Forbidden:
	case (int) Status::Status404_Not_Found:
	case (int) Status::Status405_Method_Not_Allowed:
	case (int) Status::Status406_Not_Acceptable:
	case (int) Status::Status407_Proxy_Authentication_Required:
	case (int) Status::Status408_Request_Timeout:
	case (int) Status::Status409_Conflict:
	case (int) Status::Status410_Gone:
	case (int) Status::Status411_Length_Required:
	case (int) Status::Status412_Precondition_Failed:
	case (int) Status::Status413_Payload_Too_Large:
	case (int) Status::Status414_URI_Too_Long:
	case (int) Status::Status415_Unsupported_Media_Type:
	case (int) Status::Status416_Range_Not_Satisfiable:
	case (int) Status::Status417_Expectation_Failed:
	case (int) Status::Status421_Misdirected_Request:
	case (int) Status::Status422_Unprocessable_Entity:
	case (int) Status::Status423_Locked:
	case (int) Status::Status424_Failed_Dependency:
	case (int) Status::Status425_Unassigned:
	case (int) Status::Status426_Upgrade_Required:
	case (int) Status::Status427_Unassigned:
	case (int) Status::Status428_Precondition_Required:
	case (int) Status::Status429_Too_Many_Requests:
	case (int) Status::Status430_Unassigned:
	case (int) Status::Status431_Request_Header_Fields_Too_Large:
	case (int) Status::Status500_Internal_Server_Error:
	case (int) Status::Status501_Not_Implemented:
	case (int) Status::Status502_Bad_Gateway:
	case (int) Status::Status503_Service_Unavailable:
	case (int) Status::Status504_Gateway_Timeout:
	case (int) Status::Status505_HTTP_Version_Not_Supported:
	case (int) Status::Status506_Variant_Also_Negotiates:
	case (int) Status::Status507_Insufficient_Storage:
	case (int) Status::Status508_Loop_Detected:
	case (int) Status::Status509_Unassigned:
	case (int) Status::Status510_Not_Extended:
	case (int) Status::Status511_Network_Authentication_Required:
		return true;
	}
	return false;
}

/*
This is an attempted workaround for a crash that we're seeing in production, with the following callstack:

libeay32.dll!sha1_block_data_order(SHAstate_st * c, const void * p, unsigned __int64 num) Line 280	C
libeay32.dll!SHA1_Update(SHAstate_st * c, const void * data_, unsigned __int64 len) Line 351	C
libeay32.dll!ssleay_rand_add(const void * buf, int num, double add) Line 274	C
libeay32.dll!RAND_add(const void * buf, int num, double entropy) Line 153	C
libeay32.dll!RAND_poll() Line 521	C
libeay32.dll!ssleay_rand_status() Line 579	C
libcurl.dll!Curl_ossl_seed(Curl_easy * data) Line 242	C
libcurl.dll!ossl_connect_step1(connectdata * conn, int sockindex) Line 1868	C
libcurl.dll!ossl_connect_common(connectdata * conn, int sockindex, bool nonblocking, bool * done) Line 3070	C
libcurl.dll!Curl_ssl_connect_nonblocking(connectdata * conn, int sockindex, bool * done) Line 261	C
libcurl.dll!https_connecting(connectdata * conn, bool * done) Line 1410	C
libcurl.dll!multi_runsingle(Curl_multi * multi, curltime now, Curl_easy * data) Line 1622	C
libcurl.dll!curl_multi_perform(Curl_multi * multi, int * running_handles) Line 2166	C
libcurl.dll!easy_transfer(Curl_multi * multi) Line 708	C
libcurl.dll!easy_perform(Curl_easy * data, bool events) Line 794	C
pal.dll!imqs::http::Connection::Perform(const imqs::http::Request & request, imqs::http::Response & response)	C++
MapServer.exe!imqs::maps::TileReprojector::HttpFetchThread(imqs::maps::TileReprojector::HttpRunner * self)	C++
MapServer.exe!std::_LaunchPad<std::unique_ptr<std::tuple<void(__cdecl*)(imqs::maps::TileReprojector::HttpRunner * __ptr64), imqs::maps::TileReprojector::HttpRunner * __ptr64>, std::default_delete<std::tuple<void(__cdecl*)(imqs::maps::TileReprojector::HttpRunner * __ptr64), imqs::maps::TileReprojector::HttpRunner * __ptr64> > > >::_Run(std::_LaunchPad<std::unique_ptr<std::tuple<void(__cdecl*)(imqs::maps::TileReprojector::HttpRunner *), imqs::maps::TileReprojector::HttpRunner *>, std::default_delete<std::tuple<void(__cdecl*)(imqs::maps::TileReprojector::HttpRunner *), imqs::maps::TileReprojector::HttpRunner *> > > > * _Ln)	C++
MapServer.exe!std::_Pad::_Call_func(void * _Data)	C++
ucrtbase.dll!thread_start<unsigned int(__cdecl*)(void * __ptr64)>()	Unknown
kernel32.dll!BaseThreadInitThunk()	Unknown
ntdll.dll!RtlUserThreadStart()	Unknown

If you look inside ssleay_rand_status, you'll see that it's checking if initialized = 1, and if not, then
it performs some kind of initialization for the RNG. What we're doing here is forcing that initialization
to occur early. I think the most likely thing is that the initialization is not guarded against multiples
threads from performing it simultaneously.

...aaaaand! now I remember about curl_global_init, and I see that we're not calling it anywhere.
However, having stepped through the code, and trying to place breakpoints inside ssleay_rand_status(),
I'm not sure that calling curl_global_init will solve our problems.
*/
IMQS_PAL_API void Initialize() {
	curl_global_init(CURL_GLOBAL_ALL);
}

IMQS_PAL_API void Shutdown() {
	curl_global_cleanup();
}

IMQS_PAL_API void SetDefaultCACertFile(std::string defaultCACertFile) {
	DefaultCACertFile = defaultCACertFile;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Cookie::ParseCookieFromServer(const char* s, size_t _len, Cookie& cookie) {
	// return fmt.Sprintf("%v=%v; Expires=%v", c.Name, c.Value, c.Expires.Format(http.TimeFormat))
	// Cookie=Dookie
	size_t len         = (size_t) _len;
	size_t keyStart    = -1;
	size_t valStart    = -1;
	bool   haveEquals  = false;
	bool   havePrimary = false;
	for (size_t i = 0; i <= len; i++) {
		if (i == len || s[i] == ';') {
			if (!haveEquals)
				return false;
			if (keyStart == -1 || valStart == -1)
				return false;
			size_t  keyLen = valStart - keyStart - 1;
			ssize_t valLen = (ssize_t)(i - valStart);
			if (len >= 2 && i == len && s[i - 1] == '\n' && s[i - 2] == '\r')
				valLen -= 2;
			if (keyLen <= 0)
				return false;
			if (!havePrimary) {
				if (valLen >= 0) {
					havePrimary = true;
					cookie.Name.assign(s + keyStart, keyLen);
					cookie.Value.assign(s + valStart, valLen);
				} else
					return false;
			} else {
				if (strncmp(s + keyStart, "Expires", 7) == 0) {
					if (valLen != 29)
						return false;
					cookie.Expires = time::Time::FromHttp(s + valStart, valLen);
				} else if (strncmp(s + keyStart, "Path", 4) == 0) {
					cookie.Path.assign(s + valStart, valLen);
				} else {
					// unrecognized field
				}
			}
			haveEquals = false;
			keyStart   = -1;
			valStart   = -1;
		} else if (s[i] == '=' && !haveEquals) {
			haveEquals = true;
			valStart   = i + 1;
		} else if (s[i] != ' ' && keyStart == -1) {
			keyStart = i;
		}
	}
	return havePrimary;
}

void Cookie::ParseCookiesFromBrowser(const char* s, size_t _len, std::vector<Cookie>* cookies) {
	// Cookie: CUSTOMER=WILE_E_COYOTE; PART_NUMBER=ROCKET_LAUNCHER_0001
	size_t len   = (size_t) _len;
	size_t start = 0;
	size_t eq    = 0;
	size_t pos   = 0;
	for (; true; pos++) {
		if (pos >= len || s[pos] == ';') {
			size_t name_len  = eq - start;
			size_t value_len = pos - eq - 1;
			if (name_len <= 0 || value_len <= 0 || eq == 0)
				return;

			cookies->push_back(Cookie());
			Cookie& c = cookies->back();
			c.Expires = time::Time();
			c.Name.assign(s + start, name_len);
			c.Value.assign(s + eq + 1, value_len);

			if (pos >= len)
				return;

			eq    = 0;
			start = pos + 2;
		} else if (s[pos] == '=' && eq == 0) {
			eq = pos;
		}
	}
	// unreachable
	IMQS_ASSERT(false);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* Request::DisableProxy = "_";

Request::Request() {
	Method              = "GET";
	Enable100Continue   = false;
	TimeoutMilliseconds = 0;
}
Request::~Request() {
}

void Request::AddCookie(const std::string& name, const std::string& value) {
	size_t ihead = 0;
	for (; ihead < Headers.size(); ihead++) {
		if (Headers[ihead].Key == "Cookie")
			break;
	}

	if (ihead == Headers.size())
		Headers.push_back(HeaderItem("Cookie", tsf::fmt("%v=%v", name, value)));
	else
		Headers[ihead].Value += tsf::fmt("; %v=%v", name, value);
}

void Request::AddCookie(const Cookie& cookie) {
	AddCookie(cookie.Name, cookie.Value);
}

void Request::SetBasicAuth(const std::string& username, const std::string& password) {
	auto val = "BASIC " + modp::b64_encode(username + ":" + password);
	SetHeader("Authorization", val.c_str());
}

HeaderItem* Request::HeaderByName(const std::string& name, bool createIfNotExist) {
	const HeaderItem* item = HeaderByName(name);
	if (!createIfNotExist || item != nullptr)
		return const_cast<HeaderItem*>(item);

	Headers.push_back(HeaderItem(name, ""));
	return &Headers.back();
}

const HeaderItem* Request::HeaderByName(const std::string& name) const {
	for (size_t i = 0; i < Headers.size(); i++) {
		if (Headers[i].Key == name)
			return &Headers[i];
	}
	return nullptr;
}

void Request::SetHeader(const std::string& name, const std::string& value) {
	HeaderByName(name, true)->Value = value;
}

void Request::RemoveHeader(const std::string& name) {
	for (size_t i = 0; i < Headers.size(); i++) {
		if (Headers[i].Key == name) {
			Headers.erase(Headers.begin() + i);
			return;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Response::Response() {
}
Response::~Response() {
}

void Response::SetStatus(Version version, Status code, const char* msg) {
	if (msg == nullptr)
		msg = StatusAndMessage(code);

	HeaderItem line;
	if (version == Version::Http2)
		line.Value = tsf::fmt("HTTP/2 %s", msg);
	else
		line.Value = tsf::fmt("HTTP/1.%d %s", version == Version::Http10 ? 0 : 1, msg);

	if (Headers.size() == 0)
		Headers.push_back(line);
	else
		Headers[0] = line;
}

void Response::SetHeader(const char* key, const char* value) {
	IMQS_ASSERT(key != nullptr && key[0] != 0);
	bool valueEmpty = value == nullptr || value[0] == 0;
	for (size_t i = 1; i < Headers.size(); i++) {
		if (Headers[i].Key == key) {
			if (valueEmpty)
				Headers.erase(Headers.begin() + i);
			else
				Headers[i].Value = value;
			return;
		}
	}
	if (valueEmpty)
		return;
	Headers.push_back(HeaderItem(key, value));
}

static char* UnsafeCAT(char* s, const char* append) {
	for (; *append;)
		*s++ = *append++;
	*s = 0;
	return s;
}

void Response::SetCacheControl(CacheDesignation des, int32_t max_age, time::Time expires) {
	if (des != CacheDesignation::None || max_age >= 0) {
		char buf[200];
		buf[0]  = 0;
		char* s = buf;
		int   n = 0;

		if (des & CacheDesignation::Public) {
			if (n)
				s = UnsafeCAT(s, ", ");
			s = UnsafeCAT(s, "public");
			n++;
		} else if (des & CacheDesignation::Private) {
			if (n)
				s = UnsafeCAT(s, ", ");
			s = UnsafeCAT(s, "private");
			n++;
		}

		if (des & CacheDesignation::MustRevalidate) {
			if (n)
				s = UnsafeCAT(s, ", ");
			s = UnsafeCAT(s, "must-revalidate");
			n++;
		}
		if (des & CacheDesignation::NoCache) {
			if (n)
				s = UnsafeCAT(s, ", ");
			s = UnsafeCAT(s, "no-cache");
			n++;
		}

		if (max_age >= 0) {
			max_age = std::min(max_age, (int32_t)(1 * 365 * 86400));
			if (n)
				s = UnsafeCAT(s, ", ");
			s = UnsafeCAT(s, "max-age=");
			imqs::ItoA((int) max_age, s, 10);
			s += strlen(s);
			n++;
		}
		SetHeader("Cache-Control", buf);
	}

	if (!expires.IsNull()) {
		char s[30];
		expires.FormatHttp(s);
		SetHeader("Expires", s);
	}
}

void Response::SetContentType(const char* contentType) {
	SetHeader("Content-Type", contentType);
}

void Response::SetContentLength(size_t bytes) {
	char s[80]; // 33 needed for 32-bit numbers
	imqs::I64toA((int64_t) bytes, s, 10);
	SetHeader("Content-Length", s);
}

void Response::SetDate(time::Time date) {
	char s[30];
	date.FormatHttp(s);
	SetHeader("Date", s);
}

std::string Response::StatusLine() const {
	return Headers.size() > 0 ? Headers[0].Value : "";
}

std::string Response::StatusCodeStr() const {
	if (Headers.size() == 0)
		return "";
	// Headers[0] is something like
	// HTTP/1.1 200 OK
	auto firstSpace = Headers[0].Value.find(' ');
	if (firstSpace == -1)
		return "";
	else
		return Headers[0].Value.substr(firstSpace + 1);
}

// It is tempting to want to return Status here, but that leaves us open to some kind of attacks,
// where a server responds with an invalid integer code. Then, other parts of our code may assume that
// wherever they see an Status enum, it is a valid enum. So this "tainted" invalid enum value
// could make it's way into other parts of the code and cause an "unimplemented switch case" panic, or
// perhaps something worse.
int Response::StatusCodeInt() const {
	return atoi(StatusCodeStr().c_str());
}

// For the reason mentioned above StatusCodeInt, we need to make sure we return an actual Status enum.
Status Response::StatusCode() const {
	int code = StatusCodeInt();
	if (IsValidStatus(code))
		return (Status) code;
	return Status::Status200_OK;
}

std::string Response::HeaderValue(const std::string& key) const {
	for (size_t i = 0; i < Headers.size(); i++) {
		if (strings::eqnocase(Headers[i].Key, key))
			return Headers[i].Value;
	}
	return "";
}

bool Response::FirstSetCookie(Cookie& cookie) const {
	for (size_t i = 0; i < Headers.size(); i++) {
		if (strings::eqnocase(Headers[i].Key, "Set-Cookie"))
			return Cookie::ParseCookieFromServer(Headers[i].Value.c_str(), Headers[i].Value.size(), cookie);
	}
	return false;
}

bool Response::FirstSetCookie(const std::string& name, Cookie& cookie) const {
	for (size_t i = 0; i < Headers.size(); i++) {
		if (strings::eqnocase(Headers[i].Key, "Set-Cookie")) {
			if (Cookie::ParseCookieFromServer(Headers[i].Value.c_str(), Headers[i].Value.size(), cookie) && cookie.Name == name)
				return true;
		}
	}
	return false;
}

Error Response::ToError() const {
	if (Is200())
		return Error();
	if (!Err.OK())
		return Err;
	auto msg = StatusCodeStr();
	if (Body != "") {
		msg += ". ";
		if (Body.size() > 40)
			msg += Body.substr(40) + "...";
		else
			msg += Body;
	}
	return Error(msg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Response Client::Get(const std::string& url, const HeaderMap& headers) {
	Connection c;
	return c.Get(url, headers);
}

Response Client::Post(const std::string& url, size_t bodyBytes, const void* body, const HeaderMap& headers) {
	Connection c;
	return c.Post(url, bodyBytes, body, headers);
}

Response Client::Post(const std::string& url, const std::string& body, const HeaderMap& headers) {
	Connection c;
	return c.Post(url, body, headers);
}

Response Client::Post(const std::string& url, const HeaderMap& headers) {
	Connection c;
	return c.Post(url, 0, nullptr, headers);
}

Response Client::Perform(const std::string& method, const std::string& url, size_t bodyBytes, const void* body, const std::string& caCertsFilePath, const HeaderMap& headers) {
	Connection c;
	return c.Perform(method, url, bodyBytes, body, caCertsFilePath, headers);
}

void Client::Perform(const Request& request, Response& response) {
	Connection c;
	c.Perform(request, response);
}

Response Client::Perform(const Request& request) {
	Response   response;
	Connection c;
	c.Perform(request, response);
	return response;
}

bool Client::IsLocalHost(const char* url) {
	imqs::url::URL u;
	if (!u.Decode(url).OK())
		return false;
	return u.Host == "127.0.0.1" || u.Host == "::1" || u.Host == "localhost";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Connection::Connection() {
	CurlC     = nullptr;
	Cancelled = false;
}

Connection::~Connection() {
	Close();
}

void Connection::Close() {
	if (CurlC != nullptr) {
		curl_easy_cleanup(CurlC);
		CurlC = nullptr;
	}
}

Response Connection::Get(const std::string& url, const HeaderMap& headers) {
	return Perform("GET", url, 0, nullptr, "", headers);
}

Response Connection::Post(const std::string& url, size_t bodyBytes, const void* body, const HeaderMap& headers) {
	return Perform("POST", url, bodyBytes, body, "", headers);
}

Response Connection::Post(const std::string& url, const std::string& body, const HeaderMap& headers) {
	return Perform("POST", url, body.size(), body.data(), "", headers);
}

Response Connection::Post(const std::string& url, const HeaderMap& headers) {
	return Perform("POST", url, 0, nullptr, "", headers);
}

Response Connection::Perform(const std::string& method, const std::string& url, size_t bodyBytes, const void* body, const std::string& caCertsFilePath, const HeaderMap& headers) {
	Request request;
	request.Method = method;
	if (bodyBytes)
		request.Body.assign((const char*) body, bodyBytes);
	request.Url        = url;
	request.CACertFile = caCertsFilePath;

	for (auto& it : headers)
		request.Headers.push_back(HeaderItem(it.first, it.second));

	Response response;
	Perform(request, response);
	return response;
}

void Connection::Perform(const Request& request, Response& response) {
	response = Response();
	if (request.Method != "GET" &&
	    request.Method != "HEAD" &&
	    request.Method != "POST" &&
	    request.Method != "PUT" &&
	    request.Method != "DELETE" &&  // untested
	    request.Method != "TRACE" &&   // untested
	    request.Method != "CONNECT" && // untested
	    request.Method != "PATCH" &&   // untested
	    request.Method != "OPTIONS"    // untested
	) {
		IMQS_DIE_MSG((std::string("Client: Invalid HTTP verb ") + request.Method).c_str());
	}

	CURLcode res = CURLE_COULDNT_CONNECT;
	if (CurlC == nullptr) {
		CurlC = curl_easy_init();
		if (CurlC == nullptr)
			IMQS_DIE_MSG("curl_easy_init failed");
	}
	// reset any state that we may have set on the last request
	curl_easy_setopt(CurlC, CURLOPT_CUSTOMREQUEST, nullptr);
	curl_easy_setopt(CurlC, CURLOPT_HEADER, nullptr);
	curl_easy_setopt(CurlC, CURLOPT_POSTFIELDSIZE, 0);
	curl_easy_setopt(CurlC, CURLOPT_POSTFIELDS, nullptr);
	curl_easy_setopt(CurlC, CURLOPT_UPLOAD, 0);
	curl_easy_setopt(CurlC, CURLOPT_INFILESIZE, 0);

	ReadPtr         = (uint8_t*) &request.Body[0];
	CurrentResponse = &response;

	std::string caCert = request.CACertFile;
	if (caCert == "" && DefaultCACertFile != "")
		caCert = DefaultCACertFile;

	// initialize with this new request's parameters
	curl_easy_setopt(CurlC, CURLOPT_URL, request.Url.c_str());
	curl_easy_setopt(CurlC, CURLOPT_READFUNCTION, CurlMyRead);
	curl_easy_setopt(CurlC, CURLOPT_WRITEFUNCTION, CurlMyWrite);
	curl_easy_setopt(CurlC, CURLOPT_HEADERFUNCTION, CurlMyHeaders);
	curl_easy_setopt(CurlC, CURLOPT_READDATA, this);
	curl_easy_setopt(CurlC, CURLOPT_WRITEDATA, this);
	curl_easy_setopt(CurlC, CURLOPT_HEADERDATA, this);
	curl_easy_setopt(CurlC, CURLOPT_TIMEOUT_MS, request.TimeoutMilliseconds);
	curl_easy_setopt(CurlC, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(CurlC, CURLOPT_CAINFO, caCert != "" ? caCert.c_str() : nullptr);
	curl_easy_setopt(CurlC, CURLOPT_ACCEPT_ENCODING, ""); // empty string = all supported encodings

	// This is useful if you're struggling with weird connections and proxies
	// curl_easy_setopt(CurlC, CURLOPT_VERBOSE, (long) 1);

	// libcurl makes no attempt to detect if the hostname is a loopback one such as localhost or 127.0.0.1. If http_proxy
	// is set, then it will attempt to use that, even for loopback addresses. That is why we need to take the pains here
	// to disable that.
	if (request.Proxy == Request::DisableProxy || Client::IsLocalHost(request.Url.c_str()))
		curl_easy_setopt(CurlC, CURLOPT_PROXY, "");
	else
		curl_easy_setopt(CurlC, CURLOPT_PROXY, request.Proxy != "" ? request.Proxy.c_str() : nullptr);

	curl_slist* headers = nullptr;
	if (request.Method == "POST") {
		curl_easy_setopt(CurlC, CURLOPT_POSTFIELDSIZE, request.Body.size());
		curl_easy_setopt(CurlC, CURLOPT_POSTFIELDS, (const void*) request.Body.c_str());
		//curl_easy_setopt( CurlC, CURLOPT_POST, 1 );
	} else if (request.Method == "PUT") {
		curl_easy_setopt(CurlC, CURLOPT_UPLOAD, 1);
		curl_easy_setopt(CurlC, CURLOPT_INFILESIZE_LARGE, (curl_off_t) request.Body.size());
	}
	for (size_t i = 0; i < request.Headers.size(); i++)
		headers = curl_slist_append(headers, tsf::fmt("%v: %v", request.Headers[i].Key, request.Headers[i].Value).c_str());

	if (request.Method == "POST" ||
	    request.Method == "PUT") {
		if (!request.Enable100Continue) {
			// Curl will inject an "Expect: 100-continue" header for POST and PUT data, because
			// this is part of the recommendation of HTTP 1.1: You first do a handshake, and possibly wait
			// for a 302: Redirect, before transmitting your POST or PUT data. I have never had a need for that,
			// and it doesn't seem very REST-ish.
			headers = curl_slist_append(headers, tsf::fmt("%v: %v", "Expect", "").c_str());
		} else {
			// I think you'll have to do two roundtrips here to support this concept
			IMQS_DIE_MSG("This technique is not implemented (100 Continue in http::Client)");
		}
	}

	// The following technique is untested
	if (request.Method == "HEAD" ||
	    request.Method == "DELETE" ||
	    request.Method == "TRACE" ||
	    request.Method == "CONNECT" ||
	    request.Method == "PATCH" ||
	    request.Method == "OPTIONS") {
		curl_easy_setopt(CurlC, CURLOPT_CUSTOMREQUEST, request.Method.c_str());
	}

	if (headers)
		curl_easy_setopt(CurlC, CURLOPT_HTTPHEADER, headers);

	// Fire off the request
	res = curl_easy_perform(CurlC);

	switch (res) {
	case CURLE_OK: response.Err = Error(); break;
	case CURLE_COULDNT_RESOLVE_PROXY: response.Err = ErrResolveProxyFailed; break;
	case CURLE_COULDNT_RESOLVE_HOST: response.Err = ErrResolveHostFailed; break;
	case CURLE_COULDNT_CONNECT: response.Err = ErrConnectionFailed; break;
	case CURLE_OPERATION_TIMEDOUT: response.Err = ErrTimeout; break;
	case CURLE_TOO_MANY_REDIRECTS: response.Err = ErrTooManyRedirects; break;
	case CURLE_SSL_CACERT: response.Err = Error::Fmt("Invalid CA certificate for HTTPS"); break;
	default:
		response.Err = Error::Fmt("libcurl error %v", res);
		break;
	}

	// I have seen this happen when Vipre AV's "Bad Website Blocking" is enabled
	if (response.Err.OK() && response.StatusLine().size() == 0)
		response.Err = ErrNoHeadersInResponse;

	if (headers)
		curl_easy_setopt(CurlC, CURLOPT_HTTPHEADER, nullptr);
	curl_slist_free_all(headers);

	ReadPtr         = nullptr;
	CurrentResponse = nullptr;
}

Response Connection::Perform(const Request& request) {
	Response response;
	Perform(request, response);
	return response;
}

void Connection::Cancel() {
	Cancelled = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

size_t Connection::CurlMyRead(void* ptr, size_t size, size_t nmemb, void* data) {
	Connection* self = (Connection*) data;
	if (self->Cancelled)
		return 0;
	size_t tot = size * nmemb;
	memcpy(ptr, self->ReadPtr, tot);
	self->ReadPtr += tot;
	return tot;
}

size_t Connection::CurlMyWrite(void* ptr, size_t size, size_t nmemb, void* data) {
	Connection* self = (Connection*) data;
	if (self->Cancelled)
		return 0;
	size_t tot = size * nmemb;
	self->CurrentResponse->Body.append((const char*) ptr, tot);
	return tot;
}

size_t Connection::CurlMyHeaders(void* ptr, size_t size, size_t nmemb, void* data) {
	Connection* self = (Connection*) data;
	if (self->Cancelled)
		return 0;
	size_t      tot      = size * nmemb;
	size_t      mytot    = tot;
	size_t      valStart = 0;
	const char* line     = (const char*) ptr;

	// discard the closing \r\n from the header line
	if (mytot >= 2 && line[mytot - 2] == '\r' && line[mytot - 1] == '\n')
		mytot -= 2;

	if (mytot != 0) {
		self->CurrentResponse->Headers.push_back(HeaderItem());
		HeaderItem& item = self->CurrentResponse->Headers.back();
		for (size_t i = 0; i < mytot; i++) {
			if (valStart == 0 && line[i] == ':') {
				valStart = i + 1;
				item.Key.assign(line, i);
				break;
			}
		}

		// skip whitespace at start of value (ie Content-Length: 123) - skip the space before the 123.
		for (; valStart < mytot && line[valStart] == ' '; valStart++) {
		}

		item.Value.assign(line + valStart, mytot - valStart);
	}
	return tot;
}

} // namespace http
} // namespace imqs