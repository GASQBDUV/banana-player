// A small JSON writer for the web API (bpcore, no window). Commas and quoting are handled here.
#pragma once
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace bp
{
	// Strings are escaped; bytes that aren't UTF-8 become U+FFFD; control characters come out as \u00XX.
	// NaN and infinity are written as null. key() goes before each value inside an object.
	class JsonWriter
	{
	public:
		JsonWriter& obj();
		JsonWriter& arr();
		JsonWriter& end();	// closes the innermost object or array
		JsonWriter& key(const std::string& k);
		JsonWriter& str(const std::string& s);
		JsonWriter& num(double v);
		template <class T, class = typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value>::type>
		JsonWriter& num(T v) { return numInt(int64_t(v)); }
		JsonWriter& boolean(bool b);
		JsonWriter& null();
		JsonWriter& raw(const std::string& json);	// already valid JSON, put in as is
		const std::string& out() const { return out_; }

		static std::string escape(const std::string& s);	// the quoted string

	private:
		JsonWriter& numInt(int64_t v);
		void pre();
		std::string out_;
		std::vector<char> closers_;
		std::vector<bool> any_;	// per open container: has a value
		bool afterKey_ = false;
	};
}
