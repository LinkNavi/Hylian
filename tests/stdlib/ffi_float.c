// C side of the float-FFI test: a function Hylian calls, which in turn calls
// back into a Hylian function, both passing/returning floats per the SysV ABI.
double hy_scale(double a, int n, double b, float f);   /* defined in ffi_float.hy */

double c_mix(double a, int n, double b, float f) { return a * 100 + n * 10 + b + f / 4; }

double c_call_back(double x) { return hy_scale(x, 3, 0.5, 2.0f) + 1000; }

int c_neg(int x) { return -x; }                       /* upper half of rax is not sign-extended */
unsigned int c_u32(unsigned int v) { return v + 1u; } /* wraps at 32 bits */
float c_halff(float v) { return v / 2.0f; }
