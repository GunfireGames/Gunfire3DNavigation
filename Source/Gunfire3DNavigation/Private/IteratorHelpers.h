// Copyright Gunfire Games, LLC. All Rights Reserved.

#pragma once

//
// A helper for returning the values in a map as a ranged for iterator without
// duplicating the values into an array.
//
// Usage:
//
//	typedef FMapValueIterator<TMap<uint32, FMyClass>, TMap<uint32, FMyClass>::TRangedForIterator> FMyClassIterator;
//
//	FMyClassIterator GetMyClasses()
//	{
//		return MyClassMap;
//	}
//
//	for (FMyClass& CurClass : GetMyClasses())
//	{
//		...
//	}
//
template<typename MapType, typename IteratorType>
class FMapValueIterator
{
public:
	FMapValueIterator(MapType& MapIn) : Map(MapIn) {}

	struct FRangedForIterator
	{
		explicit FRangedForIterator(IteratorType ItIn)
			: It(ItIn)
		{}

		auto& operator*() const
		{
			return It.Value();
		}

		FRangedForIterator& operator++()
		{
			++It;
			return *this;
		}

		friend bool operator!=(const FRangedForIterator& A, const FRangedForIterator& B)
		{
			return A.It != B.It;
		}

	private:
		IteratorType It;
	};

	// STL-like iterators to enable range-based for loop support.
	FORCEINLINE FRangedForIterator		begin() { return FRangedForIterator(Map.begin()); }
	FORCEINLINE FRangedForIterator		end() { return FRangedForIterator(Map.end()); }

private:
	MapType& Map;
};

//
// A helper for only returning values in an array that pass a check.
// Todo: Should templatize the condition, right now it expects a function called IsActive.
//
template<typename ValueType>
class FConditionalRangeIterator
{
public:
	FConditionalRangeIterator(ValueType* BeginIn, ValueType* EndIn)
		: Begin(BeginIn), End(EndIn)
	{
	}

	struct FRangedForIterator
	{
		explicit FRangedForIterator(ValueType* BeginIn, ValueType* EndIn)
			: Cur(BeginIn), End(EndIn)
		{
			while (Cur != nullptr && Cur != End && !Cur->IsActive())
			{
				++Cur;
			}
		}

		ValueType& operator*() const
		{
			return *Cur;
		}

		FRangedForIterator& operator++()
		{
			++Cur;

			while (Cur != End && !Cur->IsActive())
			{
				++Cur;
			}

			return *this;
		}

		bool Valid() const
		{
			return Cur != End;
		}

		friend bool operator!=(const FRangedForIterator& A, const FRangedForIterator& B)
		{
			return A.Valid();
		}

	private:
		ValueType* Cur;
		ValueType* End;
	};

	FORCEINLINE FRangedForIterator		begin() { return FRangedForIterator(Begin, End); }
	FORCEINLINE FRangedForIterator		end() { return FRangedForIterator(nullptr, nullptr); }

private:
	ValueType* Begin;
	ValueType* End;
};
