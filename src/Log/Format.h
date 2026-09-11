#pragma once

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Log
{
	class Block
	{
	public:
		explicit Block(std::string a_title) :
			_title(std::move(a_title))
		{}

		Block& Field(std::string_view a_key, std::string_view a_value)
		{
			_fields.emplace_back(a_key, a_value);
			return *this;
		}

		Block& Field(std::string_view a_key, const char* a_value)
		{
			return Field(a_key, std::string_view(a_value && a_value[0] ? a_value : "-"));
		}

		Block& Field(std::string_view a_key, const std::string& a_value)
		{
			return Field(a_key, std::string_view(a_value));
		}

		template <class T>
			requires(!std::is_convertible_v<const T&, std::string_view>)
		Block& Field(std::string_view a_key, const T& a_value)
		{
			_fields.emplace_back(a_key, fmt::format("{}", a_value));
			return *this;
		}

		template <class... Args>
			requires(sizeof...(Args) > 0)
		Block& Field(std::string_view a_key, fmt::format_string<Args...> a_fmt, Args&&... a_args)
		{
			_fields.emplace_back(a_key, fmt::format(a_fmt, std::forward<Args>(a_args)...));
			return *this;
		}

		Block& FieldIf(bool a_keep, std::string_view a_key, std::string_view a_value)
		{
			if (a_keep) {
				Field(a_key, a_value);
			}
			return *this;
		}

		Block& Hex(std::string_view a_key, std::uint32_t a_id)
		{
			_fields.emplace_back(a_key, fmt::format("{:08X}", a_id));
			return *this;
		}

		Block& Hex(std::string_view a_key, std::uint32_t a_id, std::string_view a_label)
		{
			if (a_label.empty() || a_label == "-") {
				return Hex(a_key, a_id);
			}
			_fields.emplace_back(a_key, fmt::format("{:08X}  {}", a_id, a_label));
			return *this;
		}

		Block& Addr(std::string_view a_key, std::uintptr_t a_addr)
		{
			_fields.emplace_back(a_key, fmt::format("{:X}", a_addr));
			return *this;
		}

		[[nodiscard]] std::string Str() const
		{
			std::size_t width = 0;
			for (const auto& [key, _] : _fields) {
				width = (std::max)(width, key.size());
			}

			std::string out = _title;
			out.reserve(out.size() + _fields.size() * (width + 40));
			for (const auto& [key, value] : _fields) {
				out.append("\n  ");
				out.append(key);
				if (key.size() < width) {
					out.append(width - key.size(), ' ');
				}
				out.append("  ");
				out.append(value);
			}
			return out;
		}

	private:
		std::string                            _title;
		std::vector<std::pair<std::string, std::string>> _fields;
	};
}
