#pragma once

class __declspec(novtable) CObjRef
{
	LONG _dwRef;
protected:
	CObjRef() : _dwRef(1) { }

	virtual ~CObjRef(){}

	virtual void OnFinalRelease()
	{
		delete this;
	}

public:

	void AddRef()
	{
		InterlockedIncrementNoFence(&_dwRef);
	}

	void Release()
	{
		if (!InterlockedDecrement(&_dwRef))
		{
			OnFinalRelease();
		}
	}
};
