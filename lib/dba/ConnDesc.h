#pragma once

namespace imqs {
namespace dba {

// Database connection description (aka DSN)
class IMQS_DBA_API ConnDesc {
public:
	std::string Driver;
	std::string Host;
	std::string Database; // Filename in Sqlite
	std::string Username;
	std::string Password;
	std::string Port;

	bool        SSL = false;
	std::string SSLRootCert; // filename of trusted server CAs
	std::string SSLCert;     // filename of client cert
	std::string SSLKey;      // filename of client key

	// Parse a connection string of the form driver:hostname:port:database:username:password
	// No allowance is made for escaping a colon, but if the password has colons inside it,
	// then the function will accept it.
	Error Parse(const char* s);
	Error Parse(const nlohmann::json& s);

	void UseSSL(std::string rootCert, std::string clientCert, std::string clientKey);

	std::string ToString() const;
	std::string ToLogSafeString() const; // Return a representation that can go into logs. This has no username or password.

	bool operator==(const ConnDesc& b) const;
	bool operator!=(const ConnDesc& b) const { return !(*this == b); }
};
} // namespace dba
} // namespace imqs

namespace ohash {
template <>
inline hashkey_t gethashcode(const imqs::dba::ConnDesc& c) {
	auto s = tsf::fmt("%v|%v|%v|%v", c.Host, c.Port, c.Database, c.Username);
	return XXH32(s.c_str(), (unsigned) s.length(), 0);
}
} // namespace ohash
