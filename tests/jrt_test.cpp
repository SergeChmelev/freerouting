// Prints a transcript that must be byte-identical to tests/JrtParity.java run on a real JVM.
#include <cstdio>
#include <iostream>
#include <string>

#include "jrt/jrt.h"

using namespace jrt;

static std::string line;
static void out(const std::string& s) { line += s + " "; }
static void flush(const char* tag) {
  std::cout << tag << ": " << line << "\n";
  line.clear();
}

int main() {
  // 1. HashMap<Integer,Integer> iteration order incl. resize
  {
    auto* rnd = new Random(42);
    auto* m = new HashMap<int32_t, int32_t>();
    int32_t fixed[] = {100, 3, 17, -5, 65536, 1 << 20, 31, 47, 16, 0, 12345678};
    for (int32_t k : fixed) m->put(k, k);
    for (int i = 0; i < 40; ++i) {
      int32_t k = rnd->nextInt(1000000) - 500000;
      m->put(k, i);
    }
    for (auto* e : *m) out(str(e->getKey()));
    flush("hm_int");
    m->remove(17);
    m->remove(100);
    m->put(99, 1);
    m->put(-1, 2);
    for (auto* e : m->entryVector()) out(str(e->getKey()) + "=" + str(e->getValue()));
    flush("hm_int2");
  }
  // 2. HashMap<String,...>
  {
    auto* m = new HashMap<std::string, int32_t>();
    for (int i = 1; i <= 30; ++i) m->put("net" + str(i), i);
    m->put("GND", 0);
    m->put("VCC", 1);
    m->put("\xCE\xA9-net", 2);  // "Ω-net"
    for (auto* e : *m) out(e->getKey());
    flush("hm_str");
  }
  // 3. HashSet copy-constructed from a list
  {
    auto* l = new ArrayList<int32_t>();
    for (int i = 0; i < 25; ++i) l->add(i * 37 % 101 - 20);
    auto* s = new HashSet<int32_t>(static_cast<Collection<int32_t>*>(l));
    for (int32_t x : *s) out(str(x));
    flush("hs_copy");
    auto* m2 = new HashMap<int32_t, int32_t>(5);
    for (int i = 0; i < 9; ++i) m2->put(i * 1000 + 7, i);
    for (auto* e : *m2) out(str(e->getKey()));
    flush("hm_cap5");
  }
  // 4. PriorityQueue tie-breaking (comparator ignores last digit)
  {
    auto* pq = new PriorityQueue<int32_t>([](int32_t a, int32_t b) { return Integer::compare(a / 10, b / 10); });
    auto* rnd = new Random(7);
    for (int i = 0; i < 30; ++i) pq->offer(rnd->nextInt(100));
    pq->remove(pq->peek());
    for (int32_t x : *pq) out(str(x));
    flush("pq_layout");
    while (!pq->isEmpty()) out(str(pq->poll()));
    flush("pq_poll");
  }
  // 5. Double.toString
  {
    double ds[] = {0.1, 1.0, 100.0, 1e7, 1.0E-3, 0.001, 9999999.0, 12345.678, 1e-4, 1 / 3.0, 2 / 3.0, 1e21,
                   1.7976931348623157E308, 4.9E-324, -0.0, 123456789.0, 0.00012, -2.5e-8, 1e23, 2e22, 0.5, 1234567.0,
                   9.999999999999999E22, 100.5, 3.14159};
    for (double d : ds) out(str(d));
    flush("dtostr");
    float fs[] = {0.1f, 1.0f, 1e7f, 3.3333333f, 1e-4f};
    for (float f : fs) out(str(f));
    flush("ftostr");
  }
  // 6. Math.round / casts
  {
    double rs[] = {0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 0.49999999999999994, 1e20, -1e20, Double::NaN, 3.7, -3.7};
    for (double d : rs) out(str(Math::round(d)));
    flush("round");
    for (double d : rs) out(str(d2i(d)));
    flush("d2i");
    out(str(idiv(-7, 2)));
    out(str(-7 % 2));
    out(str(Math::floorDiv(-7, 2)));
    out(str(Math::floorMod(-7, 2)));
    out(str(Math::abs(Integer::MIN_VALUE)));
    out(str(Integer::MAX_VALUE + 1));
    out(str(Math::rint(2.5)));
    out(str(Math::rint(3.5)));
    flush("intops");
  }
  // 7. BigInteger
  {
    BigInteger a = BigInteger::ONE().shiftLeft(70).add(BigInteger(12345));
    out(str(a.doubleValue()));
    BigInteger b = BigInteger(int64_t{9007199254740993LL}).multiply(BigInteger(int64_t{3}));
    out(str(b.doubleValue()));
    BigInteger c = BigInteger(int64_t{-123456789012345LL}).multiply(BigInteger(int64_t{987654321098LL}));
    out(c.toString());
    out(str(c.doubleValue()));
    out(c.abs().sqrt().toString());
    out(BigInteger(-17).mod(BigInteger(5)).toString());
    out(BigInteger(-17).divide(BigInteger(5)).toString());
    out(BigInteger(-17).remainder(BigInteger(5)).toString());
    out(str(c.hashCode()));
    out(str(a.hashCode()));
    out(str(c.intValue()));
    out(str(c.longValue()));
    out(str(BigInteger(-1).bitLength()));
    out(str(c.bitLength()));
    flush("bigint");
  }
  // 7b. BigDecimal / BigFraction rounding
  {
    const char* ds[] = {"1.250", "-7.5", "0.000", "123.4500", "1E+3", "-0.05", "2.5", "3.5", "100"};
    for (auto* s : ds) {
      BigDecimal d{std::string(s)};
      out(d.toPlainString() + "|" + str(d.scale()) + "|" + str(d.precision()) + "|" +
          d.stripTrailingZeros().toPlainString() + "|" + str(d.stripTrailingZeros().scale()) + "|" +
          d.divide(BigDecimal(std::string("3")), 4, RoundingMode::HALF_EVEN).toPlainString() + "|" +
          d.setScale(0, RoundingMode::HALF_EVEN).toPlainString());
    }
    out(BigDecimal::valueOf(0.1).toPlainString());
    out(BigDecimal::valueOf(1e7).toPlainString());
    out(BigFraction::of(-7, 2).bigDecimalValue(0, RoundingMode::HALF_EVEN).toPlainString());
    out(BigFraction::of(5, 2).bigDecimalValue(0, RoundingMode::HALF_EVEN).toPlainString());
    out(BigFraction::of(1, 3).bigDecimalValue(5, RoundingMode::HALF_EVEN).toPlainString());
    out(BigInteger(3).modInverse(BigInteger(11)).toString());
    out(BigInteger(-3).modInverse(BigInteger(11)).toString());
    flush("bigdec");
  }
  // 8. Random + shuffle
  {
    auto* r = new Random(42);
    for (int i = 0; i < 5; ++i) out(str(r->nextInt(100)));
    out(str(r->nextDouble()));
    out(str(r->nextLong()));
    out(str(r->nextInt()));
    flush("random");
    auto* l = new ArrayList<int32_t>();
    for (int i = 0; i < 20; ++i) l->add(i);
    Collections::shuffle<int32_t>(l, new Random(7));
    for (int32_t x : *l) out(str(x));
    flush("shuffle");
  }
  // 9. String hash codes
  {
    const char* ss[] = {"", "a", "GND", "net123", "Pad_1.27x0.6", "\xCE\xA9-net", "\xF0\x9F\x98\x80"};
    for (auto* s : ss) out(str(stringHashCode(s)));
    flush("strhash");
  }
  // 10. TreeSet + LinkedHashMap + stable sort
  {
    auto* ts = new TreeSet<int32_t>(reverseOrder<int32_t>());
    for (int i = 0; i < 10; ++i) ts->add(i * 7 % 10);
    for (int32_t x : *ts) out(str(x));
    out(str(ts->higher(5)));
    out(str(ts->ceiling(5)));
    flush("treeset");
    auto* l = new ArrayList<int32_t>();
    for (int i = 0; i < 20; ++i) l->add((i * 13) % 20);
    l->sort([](int32_t a, int32_t b) { return Integer::compare(a / 5, b / 5); });
    for (int32_t x : *l) out(str(x));
    flush("stablesort");
  }
  return 0;
}
