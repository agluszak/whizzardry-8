extern "C"
{
#if defined(_WIN32)
__declspec(dllexport)
#endif
int w8_loader_fixture_symbol() { return 42; }
}
