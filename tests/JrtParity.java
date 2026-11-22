// Reference transcript for tests/jrt_test.cpp. Run: java tests/JrtParity.java
import java.math.BigInteger;
import java.util.*;

public class JrtParity {
  static StringBuilder line = new StringBuilder();

  static void out(Object s) {
    line.append(s).append(' ');
  }

  static void flush(String tag) {
    System.out.println(tag + ": " + line);
    line.setLength(0);
  }

  public static void main(String[] args) {
    {
      Random rnd = new Random(42);
      HashMap<Integer, Integer> m = new HashMap<>();
      int[] fixed = {100, 3, 17, -5, 65536, 1 << 20, 31, 47, 16, 0, 12345678};
      for (int k : fixed) m.put(k, k);
      for (int i = 0; i < 40; ++i) {
        int k = rnd.nextInt(1000000) - 500000;
        m.put(k, i);
      }
      for (var e : m.entrySet()) out(e.getKey());
      flush("hm_int");
      m.remove(17);
      m.remove(100);
      m.put(99, 1);
      m.put(-1, 2);
      for (var e : m.entrySet()) out(e.getKey() + "=" + e.getValue());
      flush("hm_int2");
    }
    {
      HashMap<String, Integer> m = new HashMap<>();
      for (int i = 1; i <= 30; ++i) m.put("net" + i, i);
      m.put("GND", 0);
      m.put("VCC", 1);
      m.put("Ω-net", 2);
      for (var e : m.entrySet()) out(e.getKey());
      flush("hm_str");
    }
    {
      ArrayList<Integer> l = new ArrayList<>();
      for (int i = 0; i < 25; ++i) l.add(i * 37 % 101 - 20);
      HashSet<Integer> s = new HashSet<>(l);
      for (int x : s) out(x);
      flush("hs_copy");
      HashMap<Integer, Integer> m2 = new HashMap<>(5);
      for (int i = 0; i < 9; ++i) m2.put(i * 1000 + 7, i);
      for (var e : m2.entrySet()) out(e.getKey());
      flush("hm_cap5");
    }
    {
      PriorityQueue<Integer> pq = new PriorityQueue<>((a, b) -> Integer.compare(a / 10, b / 10));
      Random rnd = new Random(7);
      for (int i = 0; i < 30; ++i) pq.offer(rnd.nextInt(100));
      pq.remove(pq.peek());
      for (int x : pq) out(x);
      flush("pq_layout");
      while (!pq.isEmpty()) out(pq.poll());
      flush("pq_poll");
    }
    {
      double[] ds = {0.1, 1.0, 100.0, 1e7, 1.0E-3, 0.001, 9999999.0, 12345.678, 1e-4, 1 / 3.0, 2 / 3.0, 1e21,
          1.7976931348623157E308, 4.9E-324, -0.0, 123456789.0, 0.00012, -2.5e-8, 1e23, 2e22, 0.5, 1234567.0,
          9.999999999999999E22, 100.5, 3.14159};
      for (double d : ds) out(d);
      flush("dtostr");
      float[] fs = {0.1f, 1.0f, 1e7f, 3.3333333f, 1e-4f};
      for (float f : fs) out(f);
      flush("ftostr");
    }
    {
      double[] rs = {0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 0.49999999999999994, 1e20, -1e20, Double.NaN, 3.7, -3.7};
      for (double d : rs) out(Math.round(d));
      flush("round");
      for (double d : rs) out((int) d);
      flush("d2i");
      out(-7 / 2);
      out(-7 % 2);
      out(Math.floorDiv(-7, 2));
      out(Math.floorMod(-7, 2));
      out(Math.abs(Integer.MIN_VALUE));
      out(Integer.MAX_VALUE + 1);
      out(Math.rint(2.5));
      out(Math.rint(3.5));
      flush("intops");
    }
    {
      BigInteger a = BigInteger.ONE.shiftLeft(70).add(BigInteger.valueOf(12345));
      out(a.doubleValue());
      BigInteger b = BigInteger.valueOf(9007199254740993L).multiply(BigInteger.valueOf(3));
      out(b.doubleValue());
      BigInteger c = BigInteger.valueOf(-123456789012345L).multiply(BigInteger.valueOf(987654321098L));
      out(c);
      out(c.doubleValue());
      out(c.abs().sqrt());
      out(BigInteger.valueOf(-17).mod(BigInteger.valueOf(5)));
      out(BigInteger.valueOf(-17).divide(BigInteger.valueOf(5)));
      out(BigInteger.valueOf(-17).remainder(BigInteger.valueOf(5)));
      out(c.hashCode());
      out(a.hashCode());
      out(c.intValue());
      out(c.longValue());
      out(BigInteger.valueOf(-1).bitLength());
      out(c.bitLength());
      flush("bigint");
    }
    {
      Random r = new Random(42);
      for (int i = 0; i < 5; ++i) out(r.nextInt(100));
      out(r.nextDouble());
      out(r.nextLong());
      out(r.nextInt());
      flush("random");
      ArrayList<Integer> l = new ArrayList<>();
      for (int i = 0; i < 20; ++i) l.add(i);
      Collections.shuffle(l, new Random(7));
      for (int x : l) out(x);
      flush("shuffle");
    }
    {
      String[] ss = {"", "a", "GND", "net123", "Pad_1.27x0.6", "Ω-net", "😀"};
      for (String s : ss) out(s.hashCode());
      flush("strhash");
    }
    {
      TreeSet<Integer> ts = new TreeSet<>(Comparator.reverseOrder());
      for (int i = 0; i < 10; ++i) ts.add(i * 7 % 10);
      for (int x : ts) out(x);
      out(ts.higher(5));
      out(ts.ceiling(5));
      flush("treeset");
      ArrayList<Integer> l = new ArrayList<>();
      for (int i = 0; i < 20; ++i) l.add((i * 13) % 20);
      l.sort((a, b) -> Integer.compare(a / 5, b / 5));
      for (int x : l) out(x);
      flush("stablesort");
    }
  }
}
