#include "pch.h"
#include "ConnDesc.h"

using namespace std;

namespace imqs {
namespace dba {

Error ConnDesc::Parse(const char* s) {
	// with certificates: driver:hostname:port:database:username:password:(server-cert:client-cert:client-key)
	// This format is quite specific to Postgres, when using SSL. But if we ever need other things for other
	// database types, then we can see how best to fit that in if and when.
	string str = s;
	size_t p1  = str.rfind(":(");
	size_t p2  = str.rfind(")");
	if (p1 != -1 && p2 == str.size() - 1 && p2 > p1) {
		auto certs = strings::Split(str.substr(p1 + 2, p2 - p1 - 2), ':');
		if (certs.size() == 3) {
			UseSSL(certs[0], certs[1], certs[2]);
			str = str.substr(0, p1);
		}
	}

	// without certificates: driver:hostname:port:database:username:password
	auto parts = strings::Split(str, ':');
	if (parts.size() < 6) {
		while (parts.size() > 4)
			parts.pop_back();
		auto cleaned = strings::Join(parts, ":") + "...";
		return Error(tsf::fmt("Invalid database connection string: %v", cleaned));
	}

	Driver   = parts[0];
	Host     = parts[1];
	Port     = parts[2];
	Database = parts[3];
	Username = parts[4];
	Password = parts[5];
	for (size_t i = 6; i < parts.size(); i++)
		Password += ":" + parts[i];

	return Error();
}

Error ConnDesc::Parse(const nlohmann::json& s) {
	/*
	{
		"driver": "postgres",
		"host": "localhost",
		"port": "5432",
		"alias": "",
		"name": "main",
		"username": "imqs",
		"password": "supersecretpassword"
	}
	*/
	// Check for all required keys
	const std::vector<std::string> jsonKeys = {"driver", "host", "port", "name", "username", "password"};
	for (const auto& key : jsonKeys)
		if (s.find(key) == s.end())
			return Error::Fmt("missing `%v` key in json connection description", key);

	Driver   = s["driver"];
	Host     = s["host"];
	Port     = s["port"];
	Database = s["name"];
	Username = s["username"];
	Password = s["password"];

	return Error();
}

void ConnDesc::UseSSL(std::string rootCert, std::string clientCert, std::string clientKey) {
	SSL         = true;
	SSLRootCert = rootCert;
	SSLCert     = clientCert;
	SSLKey      = clientKey;
}

std::string ConnDesc::ToString() const {
	return Driver + ":" + Host + ":" + Port + ":" + Database + ":" + Username + ":" + Password;
}

std::string ConnDesc::ToLogSafeString() const {
	string s = Driver + ":" + Host + ":" + Port + ":" + Database;
	if (SSL)
		s += ":(ssl)";
	return s;
}

bool ConnDesc::operator==(const ConnDesc& b) const {
	return Driver == b.Driver &&
	       Host == b.Host &&
	       Database == b.Database &&
	       Username == b.Username &&
	       Password == b.Password &&
	       Port == b.Port;
}
} // namespace dba
} // namespace imqs
