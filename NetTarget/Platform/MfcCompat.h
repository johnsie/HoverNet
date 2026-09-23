#ifndef HOVERNET_MFC_COMPAT_H
#define HOVERNET_MFC_COMPAT_H

#include <cstdarg>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <list>
#include <map>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

typedef int BOOL;
typedef std::uint8_t BYTE;
typedef std::uint16_t WORD;
typedef std::uint32_t DWORD;
typedef unsigned int UINT;
typedef std::int32_t LONG;
typedef std::uint64_t ULONGLONG;
typedef const char* LPCSTR;
typedef const char* LPCTSTR;
typedef void* POSITION;
typedef void* HMODULE;
typedef void* HWND;

typedef std::mutex CRITICAL_SECTION;

constexpr BOOL TRUE = 1;
constexpr BOOL FALSE = 0;

#ifndef NULL
#define NULL 0
#endif

#define ASSERT(expression) ((void)0)
#define ASSERT_VALID(object) ((void)0)
#define DEBUG_NEW new
#define TRACE(...) std::fprintf(stderr, __VA_ARGS__)
#define TRY try
#define CATCH_ALL(exception) catch (...) { CException compatException; CException* exception = &compatException;
#define END_CATCH_ALL }
#define __try try
#define __except(filter) catch (...)
#define EXCEPTION_EXECUTE_HANDLER 1

class CException {
public:
    void Delete() {}
};
class CFileException : public CException {
public:
    enum Cause { fileNotFound };
};
class CMemoryException : public CException {};
class CArchiveException : public CException {
public:
    enum Cause { badSchema };
};

[[noreturn]] inline void AfxThrowFileException(CFileException::Cause)
{
    throw CFileException();
}

[[noreturn]] inline void AfxThrowArchiveException(CArchiveException::Cause)
{
    throw CArchiveException();
}

inline DWORD timeGetTime()
{
    const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

inline DWORD GetTickCount()
{
    return timeGetTime();
}

[[noreturn]] inline void AfxThrowNotSupportedException()
{
    throw std::runtime_error("Operation is not supported");
}

inline std::int64_t Int32x32To64(std::int32_t left, std::int32_t right)
{
    return static_cast<std::int64_t>(left) * static_cast<std::int64_t>(right);
}

inline std::int64_t Int64ShraMod32(std::int64_t value, int shift)
{
    return value >> shift;
}

inline int MulDiv(int number, int numerator, int denominator)
{
    return denominator == 0 ? 0 : static_cast<int>((static_cast<std::int64_t>(number) * numerator) / denominator);
}

inline HMODULE LoadLibrary(const char* fileName)
{
    return dlopen(fileName, RTLD_NOW | RTLD_LOCAL);
}

inline void* GetProcAddress(HMODULE module, const char* symbol)
{
    return dlsym(module, symbol);
}

inline BOOL FreeLibrary(HMODULE module)
{
    return dlclose(module) == 0 ? TRUE : FALSE;
}

inline void InitializeCriticalSection(CRITICAL_SECTION*) {}
inline void DeleteCriticalSection(CRITICAL_SECTION*) {}
inline void EnterCriticalSection(CRITICAL_SECTION* section) { section->lock(); }
inline void LeaveCriticalSection(CRITICAL_SECTION* section) { section->unlock(); }

template<typename TValue, typename TArgument = const TValue&>
class CList
{
private:
    struct Node {
        TValue value;
        Node* next;
        Node* previous;
    };

public:
    ~CList() { RemoveAll(); }

    int GetCount() const { return mCount; }
    POSITION GetHeadPosition() const { return mHead; }

    TValue& GetNext(POSITION& position)
    {
        Node* node = static_cast<Node*>(position);
        position = node != nullptr ? node->next : nullptr;
        return node->value;
    }

    const TValue& GetNext(POSITION& position) const
    {
        const Node* node = static_cast<const Node*>(position);
        position = node != nullptr ? node->next : nullptr;
        return node->value;
    }

    void AddTail(TArgument value)
    {
        Node* node = new Node{value, nullptr, mTail};
        if (mTail != nullptr) {
            mTail->next = node;
        }
        else {
            mHead = node;
        }
        mTail = node;
        ++mCount;
    }

    void RemoveAll()
    {
        while (mHead != nullptr) {
            Node* node = mHead;
            mHead = node->next;
            delete node;
        }
        mTail = nullptr;
        mCount = 0;
    }

private:
    Node* mHead = nullptr;
    Node* mTail = nullptr;
    int mCount = 0;
};

template<typename TKey, typename TKeyArgument, typename TValue, typename TValueArgument>
class CMap
{
private:
    typedef std::map<TKey, TValue> Storage;

    struct IteratorState {
        typename Storage::iterator iterator;
        typename Storage::iterator end;
    };

public:
    POSITION GetStartPosition()
    {
        if (mValues.empty()) {
            return nullptr;
        }
        return new IteratorState{mValues.begin(), mValues.end()};
    }

    void GetNextAssoc(POSITION& position, TKey& key, TValue& value)
    {
        IteratorState* state = static_cast<IteratorState*>(position);
        key = state->iterator->first;
        value = state->iterator->second;
        ++state->iterator;
        if (state->iterator == state->end) {
            delete state;
            position = nullptr;
        }
    }

    BOOL Lookup(TKeyArgument key, TValue& value) const
    {
        const typename Storage::const_iterator iterator = mValues.find(key);
        if (iterator == mValues.end()) {
            return FALSE;
        }
        value = iterator->second;
        return TRUE;
    }

    void SetAt(TKeyArgument key, TValueArgument value)
    {
        mValues[key] = value;
    }

    BOOL RemoveKey(TKeyArgument key)
    {
        return mValues.erase(key) == 1 ? TRUE : FALSE;
    }

    int GetCount() const
    {
        return static_cast<int>(mValues.size());
    }

private:
    Storage mValues;
};

class CObject
{
public:
    virtual ~CObject() {}
    virtual void Serialize(class CArchive&) {}
};

class CFile
{
public:
    enum OpenFlags {
        modeRead = 0x0000,
        modeWrite = 0x0001,
        modeReadWrite = 0x0002,
        modeCreate = 0x1000,
        typeBinary = 0x2000,
        shareExclusive = 0x0010,
        shareDenyWrite = 0x0020
    };

    enum SeekPosition { begin = 0, current = 1, end = 2 };

    virtual ~CFile() { Close(); }

    virtual BOOL Open(LPCTSTR fileName, UINT openFlags, CFileException* = NULL)
    {
        Close();
        if (fileName == NULL) {
            return FALSE;
        }

        std::ios::openmode mode = std::ios::binary;
        if ((openFlags & modeReadWrite) == modeReadWrite) {
            mode |= std::ios::in | std::ios::out;
            mCanRead = true;
            mCanWrite = true;
        }
        else if ((openFlags & modeWrite) == modeWrite) {
            mode |= std::ios::out;
            mCanRead = false;
            mCanWrite = true;
        }
        else {
            mode |= std::ios::in;
            mCanRead = true;
            mCanWrite = false;
        }
        if ((openFlags & modeCreate) != 0) {
            mode |= std::ios::trunc;
        }

        mStream.open(fileName, mode);
        return mStream.is_open() ? TRUE : FALSE;
    }

    virtual void Close()
    {
        if (mStream.is_open()) {
            mStream.close();
        }
        mCanRead = false;
        mCanWrite = false;
    }

    virtual ULONGLONG GetPosition() const
    {
        if (!mStream.is_open()) {
            return 0;
        }
        std::streampos position = mCanRead ? mStream.tellg() : std::streampos(-1);
        if (position == std::streampos(-1) && mCanWrite) {
            position = mStream.tellp();
        }
        return position == std::streampos(-1) ? 0 : static_cast<ULONGLONG>(position);
    }

    virtual LONG Seek(LONG offset, UINT from)
    {
        std::ios::seekdir direction = std::ios::beg;
        if (from == current) {
            direction = std::ios::cur;
        }
        else if (from == end) {
            direction = std::ios::end;
        }
        mStream.clear();
        if (mCanRead) {
            mStream.seekg(offset, direction);
        }
        if (mCanWrite) {
            mStream.seekp(offset, direction);
        }
        return static_cast<LONG>(CFile::GetPosition());
    }

    virtual ULONGLONG GetLength() const
    {
        if (!mStream.is_open()) {
            return 0;
        }
        const ULONGLONG position = CFile::GetPosition();
        mStream.clear();
        if (mCanRead) {
            mStream.seekg(0, std::ios::end);
        }
        if (mCanWrite) {
            mStream.seekp(0, std::ios::end);
        }
        const ULONGLONG length = CFile::GetPosition();
        if (mCanRead) {
            mStream.seekg(static_cast<std::streamoff>(position), std::ios::beg);
        }
        if (mCanWrite) {
            mStream.seekp(static_cast<std::streamoff>(position), std::ios::beg);
        }
        return length;
    }

    virtual UINT Read(void* buffer, UINT count)
    {
        mStream.read(static_cast<char*>(buffer), count);
        return static_cast<UINT>(mStream.gcount());
    }

    virtual void Write(const void* buffer, UINT count)
    {
        mStream.write(static_cast<const char*>(buffer), count);
    }

protected:
    mutable std::fstream mStream;
    bool mCanRead = false;
    bool mCanWrite = false;
};

class CString
{
public:
    CString() = default;
    CString(const char* value) : mValue(value != nullptr ? value : "") {}
    CString(const char* value, int length) : mValue(value != nullptr && length > 0 ? std::string(value, static_cast<size_t>(length)) : "") {}
    CString(const std::string& value) : mValue(value) {}

    operator const char*() const { return mValue.c_str(); }
    const char* GetBuffer() const { return mValue.c_str(); }
    int GetLength() const { return static_cast<int>(mValue.size()); }
    BOOL IsEmpty() const { return mValue.empty() ? TRUE : FALSE; }

    CString& operator=(const char* value)
    {
        mValue = value != nullptr ? value : "";
        return *this;
    }

    CString& operator+=(const CString& value)
    {
        mValue += value.mValue;
        return *this;
    }

    CString& operator+=(const char* value)
    {
        if (value != nullptr) {
            mValue += value;
        }
        return *this;
    }

    CString& operator+=(char value)
    {
        mValue += value;
        return *this;
    }

    char operator[](int index) const { return mValue[static_cast<size_t>(index)]; }

    int Find(char value) const
    {
        const std::string::size_type position = mValue.find(value);
        return position == std::string::npos ? -1 : static_cast<int>(position);
    }

    CString Mid(int first) const
    {
        return first >= GetLength() ? CString() : CString(mValue.substr(static_cast<size_t>(first)));
    }

    CString Left(int count) const
    {
        return CString(mValue.substr(0, static_cast<size_t>(count < 0 ? 0 : count)));
    }

    void Format(const char* format, ...)
    {
        va_list arguments;
        va_start(arguments, format);
        FormatV(format, arguments);
        va_end(arguments);
    }

    void Format(int resourceId, ...)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "resource-%d", resourceId);
        mValue = buffer;
    }

    void LoadString(UINT resourceId)
    {
        Format(static_cast<int>(resourceId));
    }

    friend CString operator+(const CString& left, const CString& right)
    {
        return CString(left.mValue + right.mValue);
    }

    friend CString operator+(const CString& left, const char* right)
    {
        return CString(left.mValue + (right != nullptr ? right : ""));
    }

    friend CString operator+(const char* left, const CString& right)
    {
        return CString((left != nullptr ? left : "") + right.mValue);
    }

private:
    void FormatV(const char* format, va_list arguments)
    {
        if (format == nullptr) {
            mValue.clear();
            return;
        }

        va_list copy;
        va_copy(copy, arguments);
        const int size = vsnprintf(nullptr, 0, format, copy);
        va_end(copy);
        if (size < 0) {
            mValue.clear();
            return;
        }

        std::vector<char> buffer(static_cast<size_t>(size) + 1);
        vsnprintf(buffer.data(), buffer.size(), format, arguments);
        mValue.assign(buffer.data(), static_cast<size_t>(size));
    }

    std::string mValue;
};

class CArchive
{
public:
    enum Mode { store = 0, load = 1, bNoFlushOnDelete = 2 };

    CArchive(CFile* file, UINT mode) : mFile(file), mStoring((mode & load) == 0) {}

    BOOL IsStoring() const { return mStoring ? TRUE : FALSE; }
    UINT Read(void* buffer, UINT count) { return mFile->Read(buffer, count); }
    void Write(const void* buffer, UINT count) { mFile->Write(buffer, count); }
    void Close() {}

    template<typename TValue>
    CArchive& operator<<(const TValue& value)
    {
        Write(&value, sizeof(value));
        return *this;
    }

    template<typename TValue>
    CArchive& operator>>(TValue& value)
    {
        if (Read(&value, sizeof(value)) != sizeof(value)) {
            std::memset(&value, 0, sizeof(value));
        }
        return *this;
    }

    CArchive& operator<<(const CString& value)
    {
        const UINT length = static_cast<UINT>(value.GetLength());
        WriteStringLength(length);
        if (length > 0) {
            Write(static_cast<const char*>(value), length);
        }
        return *this;
    }

    CArchive& operator>>(CString& value)
    {
        const UINT length = ReadStringLength();
        std::vector<char> buffer(length);
        if (length > 0 && Read(buffer.data(), length) != length) {
            value = "";
            return *this;
        }
        value = CString(buffer.data(), static_cast<int>(length));
        return *this;
    }

private:
    void WriteStringLength(UINT length)
    {
        if (length < 0xff) {
            const BYTE shortLength = static_cast<BYTE>(length);
            Write(&shortLength, sizeof(shortLength));
        }
        else {
            const BYTE marker = 0xff;
            Write(&marker, sizeof(marker));
            if (length < 0xffff) {
                const WORD mediumLength = static_cast<WORD>(length);
                Write(&mediumLength, sizeof(mediumLength));
            }
            else {
                const WORD markerLength = 0xffff;
                Write(&markerLength, sizeof(markerLength));
                Write(&length, sizeof(length));
            }
        }
    }

    UINT ReadStringLength()
    {
        BYTE shortLength = 0;
        if (Read(&shortLength, sizeof(shortLength)) != sizeof(shortLength)) {
            return 0;
        }
        if (shortLength != 0xff) {
            return shortLength;
        }

        WORD mediumLength = 0;
        if (Read(&mediumLength, sizeof(mediumLength)) != sizeof(mediumLength)) {
            return 0;
        }
        if (mediumLength != 0xffff) {
            return mediumLength;
        }

        UINT longLength = 0;
        return Read(&longLength, sizeof(longLength)) == sizeof(longLength) ? longLength : 0;
    }

    CFile* mFile;
    bool mStoring;
};

#endif