#pragma once
#include <cstdint>

template<typename T>
class TArray
{
private:
	T* _data;
	int32_t		_count;
	int32_t		_max;

	friend class FString;

public:
	TArray()
	{
		_data = nullptr;
		_count = 0;
		_max = 0;
	}

	T* Data() const
	{
		return _data;
	}

	int32_t Count() const
	{
		return _count;
	}

	int32_t Max() const
	{
		return _max;
	}

	bool IsValidIndex(int32_t i) const
	{
		return i < _count;
	}

	T& operator[](int32_t i)
	{
		return _data[i];
	}

	const T& operator[](int32_t i) const
	{
		return _data[i];
	}

	void Add(const T& Element)
	{
		if (_count >= _max)
		{

			_max = (_max == 0) ? 1 : _max + 1;

			T* NewData = new T[_max];


			for (int32_t i = 0; i < _count; ++i)
			{
				NewData[i] = _data[i];
			}


			delete[] _data;
			_data = NewData;
		}


		_data[_count++] = Element;
	}

	void Reserve(int32_t NewMax)
	{
		if (NewMax > _max)
		{
			T* NewData = new T[NewMax];


			int32_t Count = _count;
			for (int32_t i = 0; i < Count; ++i)
			{
				NewData[i] = _data[i];
			}


			delete[] _data;
			_data = NewData;
			_max = NewMax;
			_count = Count;
		}
	}
};