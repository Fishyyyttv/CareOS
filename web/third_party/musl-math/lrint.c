/* CareOS C wrapper for musl src/math/x86_64/lrint.s. */
long lrint(double x) {
    long result;
    __asm__("cvtsd2si %1, %0" : "=r"(result) : "x"(x));
    return result;
}
