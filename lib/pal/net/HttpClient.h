#pragma once

#include "../Time_.h"

namespace imqs {
namespace http {

enum class Status {
	Status100_Continue                        = 100,
	Status101_Switching_Protocols             = 101,
	Status102_Processing                      = 102,
	Status200_OK                              = 200,
	Status201_Created                         = 201,
	Status202_Accepted                        = 202,
	Status203_Non_Authoritative_Information   = 203,
	Status204_No_Content                      = 204,
	Status205_Reset_Content                   = 205,
	Status206_Partial_Content                 = 206,
	Status207_Multi_Status                    = 207,
	Status208_Already_Reported                = 208,
	Status226_IM_Used                         = 226,
	Status300_Multiple_Choices                = 300,
	Status301_Moved_Permanently               = 301,
	Status302_Found                           = 302,
	Status303_See_Other                       = 303,
	Status304_Not_Modified                    = 304,
	Status305_Use_Proxy                       = 305,
	Status307_Temporary_Redirect              = 307,
	Status308_Permanent_Redirect              = 308,
	Status400_Bad_Request                     = 400,
	Status401_Unauthorized                    = 401,
	Status402_Payment_Required                = 402,
	Status403_Forbidden                       = 403,
	Status404_Not_Found                       = 404,
	Status405_Method_Not_Allowed              = 405,
	Status406_Not_Acceptable                  = 406,
	Status407_Proxy_Authentication_Required   = 407,
	Status408_Request_Timeout                 = 408,
	Status409_Conflict                        = 409,
	Status410_Gone                            = 410,
	Status411_Length_Required                 = 411,
	Status412_Precondition_Failed             = 412,
	Status413_Payload_Too_Large               = 413,
	Status414_URI_Too_Long                    = 414,
	Status415_Unsupported_Media_Type          = 415,
	Status416_Range_Not_Satisfiable           = 416,
	Status417_Expectation_Failed              = 417,
	Status421_Misdirected_Request             = 421,
	Status422_Unprocessable_Entity            = 422,
	Status423_Locked                          = 423,
	Status424_Failed_Dependency               = 424,
	Status425_Unassigned                      = 425,
	Status426_Upgrade_Required                = 426,
	Status427_Unassigned                      = 427,
	Status428_Precondition_Required           = 428,
	Status429_Too_Many_Requests               = 429,
	Status430_Unassigned                      = 430,
	Status431_Request_Header_Fields_Too_Large = 431,
	Status500_Internal_Server_Error           = 500,
	Status501_Not_Implemented                 = 501,
	Status502_Bad_Gateway                     = 502,
	Status503_Service_Unavailable             = 503,
	Status504_Gateway_Timeout                 = 504,
	Status505_HTTP_Version_Not_Supported      = 505,
	Status506_Variant_Also_Negotiates         = 506,
	Status507_Insufficient_Storage            = 507,
	Status508_Loop_Detected                   = 508,
	Status509_Unassigned                      = 509,
	Status510_Not_Extended                    = 510,
	Status511_Network_Authentication_Required = 511,
};

IMQS_PAL_API const char* StatusAndMessage(Status code);
IMQS_PAL_API bool        IsValidStatus(int code);
IMQS_PAL_API void        Initialize();
IMQS_PAL_API void        Shutdown();
IMQS_PAL_API void        SetDefaultCACertFile(std::string defaultCACertFile);

struct IMQS_PAL_API HeaderItem {
	std::string Key;
	std::string Value;

	HeaderItem() {}
	HeaderItem(const std::string& key, const std::string& value) : Key(key), Value(value) {}
};

typedef ohash::map<std::string, std::string> HeaderMap;

struct IMQS_PAL_API Cookie {
	std::string Name;
	std::string Value;
	std::string Path;
	time::Time  Expires;

	static void ParseCookiesFromBrowser(const char* s, size_t _len, std::vector<Cookie>* cookies);
	static bool ParseCookieFromServer(const char* s, size_t _len, Cookie& cookie);
};

class IMQS_PAL_API Request {
public:
	static const char* DisableProxy;

	std::vector<HeaderItem> Headers;
	std::string             Method; // default is GET
	std::string             Url;
	std::string             Body;
	std::string             CACertFile;          // Necessary for libCurl, since it doesn't use the Windows certs by default. This is a certificate bundle file, typically with a .crt extension.
	std::string             Proxy;               // Set this to DisableProxy to force no proxy. By default, libcurl reads the http_proxy and all_proxy environment variables.
	bool                    Enable100Continue;   // Enable the use of "HTTP 100: Continue" for PUT and POST requests (default = false). Setting this to true is not yet supported.
	int32_t                 TimeoutMilliseconds; // Timeout in milliseconds. Zero is the default, which means no library-specific timeout.

	Request();
	~Request();

	static Request GET(const std::string& url) {
		Request r;
		r.Method = "GET";
		r.Url    = url;
		return r;
	}
	static Request POST(const std::string& url) {
		Request r;
		r.Method = "POST";
		r.Url    = url;
		return r;
	}

	void              AddCookie(const std::string& name, const std::string& value);
	void              AddCookie(const Cookie& cookie);
	void              SetBasicAuth(const std::string& username, const std::string& password);
	HeaderItem*       HeaderByName(const std::string& name, bool createIfNotExist);
	const HeaderItem* HeaderByName(const std::string& name) const;
	void              SetHeader(const std::string& name, const std::string& value);
	void              RemoveHeader(const std::string& name);
};

enum class Version {
	Http10 = 10, // HTTP 1.0
	Http11 = 11, // HTTP 1.1
	Http2  = 20, // HTTP 2
};

enum class CacheDesignation {
	None           = 0,
	Public         = 1,
	Private        = 2,
	MustRevalidate = 4,
	NoCache        = 8,
};

inline uint32_t         operator&(CacheDesignation a, CacheDesignation b) { return (uint32_t) a & (uint32_t) b; }
inline CacheDesignation operator|(CacheDesignation a, CacheDesignation b) { return (CacheDesignation)((uint32_t) a | (uint32_t) b); }
class IMQS_PAL_API      Response {
public:
    std::vector<HeaderItem> Headers; // Headers[0].Value is status line
    std::string             Body;    // Response body
    Error                   Err;     // Transport error (from libcurl). See also ToError(), which is more generic than this.

    Response();
    ~Response();

    /////////////////////////////////////////////////////////////////////////////////////////////////
    // Mutation
    void SetStatus(Version version, Status code, const char* msg = nullptr);
    void SetHeader(const char* key, const char* value); // If value is null or zero length, then removes the header

    // Convenience header setters
    void SetCacheControl(CacheDesignation des, int32_t max_age, time::Time expires); // If max_age is negative, then we omit it. If expires is null, then we omit it.
    void SetContentType(const char* contentType);
    void SetContentLength(size_t bytes);
    void SetDate(time::Time date);

    /////////////////////////////////////////////////////////////////////////////////////////////////
    // Inspection
    std::string HeaderValue(const std::string& key) const; // Returns an empty string if no such header exists, otherwise the first header that matches the key (case insensitive match).
    std::string StatusLine() const;                        // Returns, for example, "HTTP/1.1 401 Unauthorized"
    std::string StatusCodeStr() const;                     // Returns, for example, "401 Unauthorized"
    int         StatusCodeInt() const;                     // Returns, for example, the integer 401. Can compare against Status enum, but note that this value could be outside of Status's range.
    Status      StatusCode() const;                        // If the status not in our Status enum, returns Status200_OK.
    bool        FirstSetCookie(Cookie& cookie) const;
    bool        FirstSetCookie(const std::string& name, Cookie& cookie) const;

    bool  Is200() const { return StatusCodeInt() == (int) Status::Status200_OK; }
    Error ToError() const; // If 2xx, then return Error(). Else, if !Err.OK(), then return Err. Else, return error from status code and body.
};

// Static HTTP client functions
// The API here is purposefully identical to that of Connection
class IMQS_PAL_API Client {
public:
	friend class Connection;

	static Response Get(const std::string& url, const HeaderMap& headers = HeaderMap());
	static Response Post(const std::string& url, size_t bodyBytes, const void* body, const HeaderMap& headers = HeaderMap());
	static Response Post(const std::string& url, const HeaderMap& headers = HeaderMap());
	static Response Perform(const std::string& method, const std::string& url, size_t bodyBytes, const void* body, const std::string& caCertsFilePath = "", const HeaderMap& headers = HeaderMap());
	static void     Perform(const Request& request, Response& response);
	static Response Perform(const Request& request);
	static bool     IsLocalHost(const char* url);
};

// Stateful HTTP connection.
// Use this when you want to reuse a TCP socket for multiple HTTP requests.
// Performing any action will keep the socket alive. If you want to explicitly close the socket,
// then you must call Close().
// The destructor calls Close().
// The API here is purposefully identical to that of Client. The idea around this is that
// the only difference between using Client and Connection is that you get a stateful
// connection in the one.
class IMQS_PAL_API Connection {
public:
	Connection();
	~Connection(); // This calls Close()

	void     Close();
	Response Get(const std::string& url, const HeaderMap& headers = HeaderMap());
	Response Post(const std::string& url, size_t bodyBytes, const void* body, const HeaderMap& headers = HeaderMap());
	Response Post(const std::string& url, const HeaderMap& headers = HeaderMap());
	Response Perform(const std::string& method, const std::string& url, size_t bodyBytes, const void* body, const std::string& caCertsFilePath = "", const HeaderMap& headers = HeaderMap());
	void     Perform(const Request& request, Response& response);
	Response Perform(const Request& request);

	// Attempt to cancel the current transfer, potentially from another thread.
	// This was added in order to speed up shutdown, by aborting an operation
	// that is currently in progress. Calling this function will set IsCancelled to true,
	// which in turn will cause our Curl read/write callback functions to return 0,
	// which Curl interprets as a cancellation response. This is the officially
	// recommended mechanism for canceling a Curl transfer.
	void Cancel();
	bool IsCancelled() const { return Cancelled; }

private:
	void*             CurlC           = nullptr;
	uint8_t*          ReadPtr         = nullptr; // Only valid while a transfer is taking place
	Response*         CurrentResponse = nullptr; // Only valid while a transfer is taking place
	std::atomic<bool> Cancelled;

	static size_t CurlMyRead(void* ptr, size_t size, size_t nmemb, void* data);
	static size_t CurlMyWrite(void* ptr, size_t size, size_t nmemb, void* data);
	static size_t CurlMyHeaders(void* ptr, size_t size, size_t nmemb, void* data);
};
} // namespace http
} // namespace imqs