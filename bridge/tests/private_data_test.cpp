#include "../src/client/d3d9_privatedata.h"
#include <cstdio>
#include <cstdlib>

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

struct MockUnknown final : IUnknown {
  ULONG references = 1;
  ULONG adds = 0;
  ULONG releases = 0;
  PrivateDataFactory* callback = nullptr;
  GUID key = {};
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
  ULONG STDMETHODCALLTYPE AddRef() override { ++adds; return ++references; }
  ULONG STDMETHODCALLTYPE Release() override {
    ++releases;
    if (callback) {
      DWORD size = 0;
      CHECK(callback->getData(key, nullptr, &size) == D3DERR_NOTFOUND);
    }
    return --references;
  }
};

int main() {
  const GUID a = { 0x12345, 2, 3, {1,2,3,4,5,6,7,8} };
  const GUID b = { 0x12345, 2, 3, {1,2,3,4,5,6,7,9} };
  MockUnknown one, two;
  {
    PrivateDataFactory data;
    char source[] = "abcde";
    char output[12] = {};
    DWORD size = sizeof(output);
    CHECK(data.getData(a, output, &size) == D3DERR_NOTFOUND);
    CHECK(data.freeData(a) == D3DERR_NOTFOUND);
    CHECK(data.setData(a, source, 5, 0) == D3D_OK);
    source[0] = 'X';
    CHECK(data.getData(a, output, &size) == D3D_OK);
    CHECK(size == 5 && std::memcmp(output,"abcde",5) == 0);
    size = sizeof(output);
    CHECK(data.getData(a, output, &size) == D3D_OK);
    CHECK(std::memcmp(output,"abcde",5) == 0);
    size = 2; output[0] = 'Q';
    CHECK(data.getData(a, output, &size) == D3DERR_MOREDATA);
    CHECK(size == 5 && output[0] == 'Q');
    size = 0;
    CHECK(data.getData(a, nullptr, &size) == D3DERR_MOREDATA && size == 5);
    CHECK(data.getData(a, nullptr, &size) == D3D_OK && size == 5);
    CHECK(data.getData(a, output, nullptr) == D3DERR_INVALIDCALL);
    CHECK(data.setData(a, nullptr, 5, 0) == D3DERR_INVALIDCALL);
    CHECK(data.setData(a, source, 5, 2) == D3DERR_INVALIDCALL);
    CHECK(data.setData(a, "Z", 1, 0) == D3D_OK);
    size = sizeof(output);
    CHECK(data.getData(a, output, &size) == D3D_OK && size == 1 && output[0] == 'Z');
    CHECK(data.setData(b, "B", 1, 0) == D3D_OK);
    CHECK(data.freeData(a) == D3D_OK);
    CHECK(data.freeData(a) == D3DERR_NOTFOUND);
    size = sizeof(output);
    CHECK(data.getData(b, output, &size) == D3D_OK && output[0] == 'B');
    CHECK(data.setData(a, nullptr, 0, 0) == D3D_OK);
    size = 0;
    CHECK(data.getData(a, nullptr, &size) == D3D_OK && size == 0);
    CHECK(data.setData(a, &one, sizeof(IUnknown*), D3DSPD_IUNKNOWN) == D3D_OK);
    CHECK(one.references == 2 && one.adds == 1 && one.releases == 0);
    size = 0;
    CHECK(data.getData(a, nullptr, &size) == D3DERR_MOREDATA && size == sizeof(IUnknown*));
    CHECK(data.getData(a, nullptr, &size) == D3D_OK && one.references == 2);
    IUnknown* result = nullptr;
    CHECK(data.getData(a, &result, &size) == D3D_OK && result == &one && one.references == 3);
    result->Release();
    CHECK(one.references == 2);
    CHECK(data.setData(a, &one, 1, D3DSPD_IUNKNOWN) == D3DERR_INVALIDCALL);
    CHECK(data.setData(a, nullptr, sizeof(IUnknown*), D3DSPD_IUNKNOWN) == D3DERR_INVALIDCALL);
    CHECK(data.setData(a, &two, sizeof(IUnknown*), D3DSPD_IUNKNOWN) == D3D_OK);
    CHECK(one.references == 1 && two.references == 2);
    CHECK(data.setData(a, &two, sizeof(IUnknown*), D3DSPD_IUNKNOWN) == D3D_OK);
    CHECK(two.references == 2);
    CHECK(data.setData(a, "raw", 3, 0) == D3D_OK && two.references == 1);
    size = sizeof(output);
    CHECK(data.getData(a, output, &size) == D3D_OK && size == 3 && std::memcmp(output,"raw",3) == 0);
    CHECK(data.setData(a, &one, sizeof(IUnknown*), D3DSPD_IUNKNOWN) == D3D_OK);
    one.callback = &data; one.key = a;
    CHECK(data.freeData(a) == D3D_OK && one.references == 1);
    one.callback = nullptr;
    CHECK(data.setData(a, &two, sizeof(IUnknown*), D3DSPD_IUNKNOWN) == D3D_OK);
  }
  CHECK(one.references == 1 && two.references == 1);
  CHECK(one.adds == one.releases && two.adds == two.releases);
  std::printf("PASS: %u private-data checks\n", checks);
  return 0;
}
