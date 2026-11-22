// Reference run through the same entry point the C++ CLI uses (FreeroutingHeadlessEngine).
// run: java -cp <freerouting jar> tools/parity/HeadlessRoute.java <in.dsn> <out.ses> [maxThreads]
import app.freerouting.core.facade.FreeroutingHeadlessEngine;
import app.freerouting.core.facade.HeadlessRoutingRequest;
import app.freerouting.core.facade.HeadlessRoutingResult;
import app.freerouting.settings.RouterSettings;
import java.nio.file.Files;
import java.nio.file.Path;

public class HeadlessRoute {
  public static void main(String[] args) throws Exception {
    byte[] bytes = Files.readAllBytes(Path.of(args[0]));
    String name = Path.of(args[0]).getFileName().toString().replaceAll("\\.dsn$", "");
    var builder = HeadlessRoutingRequest.builder().dsnBytes(bytes).designName(name);
    if (args.length > 2) {
      RouterSettings s = new RouterSettings();
      s.setMaxThreads(Integer.parseInt(args[2]));
      builder.settings(s);
    }
    long start = System.nanoTime();
    HeadlessRoutingResult r = FreeroutingHeadlessEngine.route(builder.build());
    double seconds = (System.nanoTime() - start) / 1e9;
    Files.writeString(Path.of(args[1]), r.sesContent());
    int violations = r.clearanceViolations() == null ? 0 : r.clearanceViolations().size();
    System.out.printf(
        "%s.dsn: %d unrouted, %d clearance violations, %.1f s -> %s%n",
        name, r.unroutedConnections(), violations, seconds, args[1]);
  }
}
