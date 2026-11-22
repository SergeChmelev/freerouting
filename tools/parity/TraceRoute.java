// Differential-testing helper: routes a DSN with the headless engine and full granular tracing.
// run (debug build): java -Dfr.traceAll=true -cp <debug jar> tools/parity/TraceRoute.java <file.dsn>
import app.freerouting.core.facade.FreeroutingHeadlessEngine;
import app.freerouting.core.facade.HeadlessRoutingRequest;
import app.freerouting.core.facade.HeadlessRoutingResult;
import app.freerouting.logger.FRLogger;
import java.nio.file.Files;
import java.nio.file.Path;
import org.apache.logging.log4j.Level;
import org.apache.logging.log4j.core.config.Configurator;

public class TraceRoute {
  public static void main(String[] args) throws Exception {
    Configurator.setRootLevel(Level.TRACE);
    FRLogger.granularTraceEnabled = true;
    byte[] bytes = Files.readAllBytes(Path.of(args[0]));
    String name = Path.of(args[0]).getFileName().toString().replaceAll("\\.dsn$", "");
    HeadlessRoutingResult r =
        FreeroutingHeadlessEngine.route(
            HeadlessRoutingRequest.builder().dsnBytes(bytes).designName(name).build());
    System.out.println("RESULT unrouted=" + r.unroutedConnections());
  }
}
