// Differential-testing helper: dumps a board loaded from DSN by the reference Java build.
// run: java -cp <freerouting jar> tools/parity/BoardDump.java <file.dsn>
import app.freerouting.board.facade.BasicBoard;
import app.freerouting.board.model.items.Item;
import app.freerouting.geometry.planar.IntBox;
import app.freerouting.geometry.planar.TileShape;
import app.freerouting.io.BoardReadResult;
import app.freerouting.io.specctra.DsnReader;
import java.nio.file.Files;
import java.nio.file.Path;

public class BoardDump {
  public static void main(String[] args) throws Exception {
    byte[] bytes = Files.readAllBytes(Path.of(args[0]));
    BoardReadResult r = DsnReader.readBoard(bytes, null, null, "dump");
    BasicBoard b;
    if (r instanceof BoardReadResult.Success s) b = s.board();
    else if (r instanceof BoardReadResult.OutlineMissing o) b = o.board();
    else throw new IllegalStateException("read failed: " + r);
    System.out.println("angle " + b.rules.getTraceAngleRestriction());
    System.out.println("layers " + b.getLayerCount());
    for (Item it : b.getItems()) {
      StringBuilder sb = new StringBuilder();
      sb.append(it.getClass().getSimpleName()).append(" id=").append(it.getId());
      sb.append(" nets=");
      for (int i = 0; i < it.netCount(); i++) sb.append(i > 0 ? "," : "").append(it.getNetNumber(i));
      sb.append(" cl=").append(it.clearanceClassIndex());
      sb.append(" layers=").append(it.firstLayer()).append("-").append(it.lastLayer());
      sb.append(" fixed=").append(it.getFixedState());
      System.out.println(sb);
      for (int i = 0; i < it.tileShapeCount(); i++) {
        TileShape t = it.getTileShape(i);
        IntBox bb = t.boundingBox();
        System.out.println("  shape " + i + " bbox=" + bb.ll.x + "," + bb.ll.y + "," + bb.ur.x + "," + bb.ur.y
            + " " + t);
      }
    }
  }
}
