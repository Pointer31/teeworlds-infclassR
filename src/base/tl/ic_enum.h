#ifndef BASE_TL_IC_ENUM_H
#define BASE_TL_IC_ENUM_H

#include <base/system.h>

template<typename T>
concept EnumHasCount = requires { T::Count; } || requires { T::COUNT; };

template<typename T>
concept EnumHasInvalidKey = requires { T::Invalid; } || requires { T::INVALID; };

template<EnumHasInvalidKey T>
constexpr T GetEnumInvalidValue()
{
	if constexpr(requires { static_cast<int>(T::Invalid); })
		return T::Invalid;
	else
		return T::INVALID;
}

template<EnumHasCount T>
constexpr int GetEnumKeysCount()
{
	if constexpr(requires { static_cast<int>(T::Count); })
		return static_cast<int>(T::Count);
	else
		return static_cast<int>(T::COUNT);
}

template<EnumHasCount T, int NamesCount>
[[nodiscard]] const char *toStringImpl(T Value, const char *(&apNames)[NamesCount])
{
	int Index = static_cast<int>(Value);
	if((Index < 0) || (Index >= NamesCount))
	{
		// T::Invalid and T::Count are equal to avoid extra case in switch()
		// static_assert(GetEnumKeysCount<T>() + 1 == NamesCount);
		dbg_msg("ic_enum", "toStringImpl(%d): out of range!", Index);
		return apNames[static_cast<int>(GetEnumInvalidValue<T>())];
	}
	return apNames[Index];
}

template<typename T, int NamesCount>
[[nodiscard]] const char *toStringImpl(T Value, const char *(&apNames)[NamesCount], const char *pInvalidStr)
{
	int Index = static_cast<int>(Value);
	if((Index < 0) || (Index >= NamesCount))
	{
		return pInvalidStr;
	}
	return apNames[Index];
}

template<typename T>
[[nodiscard]] T fromString(const char *pString)
{
	dbg_assert(pString != nullptr, "Invalid 'fromString()' called with nullptr");
	for(int i = 0; i < GetEnumKeysCount<T>(); ++i)
	{
		const T Value = static_cast<T>(i);
		if(str_comp(pString, toString(Value)) == 0)
		{
			return Value;
		}
	}

	return GetEnumInvalidValue<T>();
}

#endif // BASE_TL_IC_ENUM_H
