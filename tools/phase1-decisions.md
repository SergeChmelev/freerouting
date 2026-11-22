# Cross-group requirements collected from phase-1 reports (feed into phase-2 prompts)

## From optimize/drc/scoring (done)
- Collections of forward-declared types: instantiate in .cpp (added to PORTING.md).
- NetRoutingLedger::maximumConnections_ (field renamed). ShoveCheckCache::CURRENT()/forThread().
- StepSlider::stepCount/isStep take Array<Point*>* — callers with IntPoint[] must convert.
- BoardStatistics: optional<> boxed fields, Unit param optional; toString hand-written.
- Wrote src/drc/UnconnectedItems.cpp already.

## From searchtree/facade (done)
- Board snapshots = BasicBoard* (serialize(bool)/deserialize) instead of byte[] (BoardHistory, AutorouteConnectionRouter).
- getHash() -> std::string canonical dump (needs a hash; any 128-bit).
- BasicBoard::routingLedger_ field rename. postLoadFuture dropped.
- `(Item) null` overload args -> static_cast<Item*>(nullptr).
- Classes implementing SearchTreeObject: add `using SearchTreeObject::isTraceObstacle;`.
- ShapeSearchTree::key recomputed in derived ctors.
- deepCopy NEEDS copy hooks from:
  - datastructures: UndoableObjects copy mapping each object, preserving levels/stacks
  - rules: BoardRules deep copy (nets->setBoard(copy))
  - board/state: Communication copy (idGenerator copied, observers null)
  - board/actions: ItemIdGenerator copy
  - autoroute: RoutingFailureLog copy
  - items: Item::copy(id) must carry all non-transient fields

## From items/trace/actions (done)
- Item : virtual SearchTreeObject, ItemInfoPrinter_Printable, UndoableObjects_Storable; compareTo(jrt::Object*) (desc id); clone() -> jrt::Object*; identity equals/hash.
- Item not Connectable; Trace/DrillItem/ConductionArea forward Connectable methods.
- Renamed fields: Item::clearanceClassIndex_, ConductionArea::isObstacle_, ComponentOutline::isFront_/isCourtyard_/isFabrication_/isClosed_.
- optional<string>: Item::componentName(), Pin::name(), ObstacleArea name (ctor param -> std::nullopt).
- ConductionArea pad-connection mode optional<PadPlaneConnection_Mode>; ForcedViaInserter attachSmdAllowed optional<bool>.
- Item::write(ObjectOutputStream) dropped -> structure's BoardOutline must NOT override write(). CHECK at integration.
- Locale -> const std::string& locale. FloatPoint toString dropped Locale param.
- .cpp written: ItemIdGenerator.cpp, PadPlaneConnection.cpp.

## From autoroute core/maze/path (done)
- Maze list = jrt::TreeSet<MazeListElement*> (Java anon TreeSet subclass overriding add -> local subclass in .cpp).
- Listeners: abstract + static X* of(std::function); events have jrt::Object* source/getSource().
- FoundConnectionLocator: TWO-PHASE init — ctor stores ctrl/searchTree/angleRestriction; initialize(result, rippedItemList, ripupCosts) called by getInstance; direct constructions must call initialize.
- ItemRouteResult fields itemId_, improved_, improvementPercentage_, incompleteCountBefore_.
- AutorouteControl::fanoutStartPinName optional<string>; RoutingFailureLog_ItemFailureInfo::lastFailureState optional.
- PerformanceProfiler statics -> accessors timings(), counts(), stack(), passHistory(), configInfo().
- BoardHistoryEntry::timestamp int64 ms; BoardHistory keeps RoutingBoard* copies, hand out fresh deep copy on restore.
- DestinationDistance box fields = IntBox::EMPTY() in ctor.
- .cpp written: AutorouteDiagnostic.cpp.

## From settings/core (done)
- Defaults: DefaultSettings.cpp (ported value-for-value); RouterSettings fields all optional/null.
- Merge: each settings class has copyNewValuesFrom(X*); RouterSettings::applyNewValuesFrom calls it; SettingsMerger stable sort by priority.
- RouterSettings private autorouter() -> autorouterOrCreate(); RoutingJob::isCancelledByUser_; HeadlessRoutingRequest_Builder fields end with _.
- RoutingJob times optional<int64_t> ms; UUIDs std::string; logError(const std::exception*); KiCad fields removed (RoutingFallbackRunner: null branch).
- StoppableThread: no Thread, run() calls threadAction().
- File/Path -> string, InputStream -> std::istream&, byte[] -> jrt::Array<int8_t>*.
- Phase 2 TODO: RouterSettings, SettingsMerger, PropertyChangeSupport, DsnFileSettings, RulesFileSettings, BoardFileDetails, RoutingJob, RoutingResultManifest, 3 facade records, HeadlessStoppableThread, FreeroutingHeadlessEngine.
- Fixed runtime: jrt::Object copy ctor resets identity hash.

## From geometry/planar (done)
- Statics: Point::ZERO(), Vector::ZERO(), FloatPoint::ZERO(), Direction::NULL_(), RIGHT()..DOWN45() (IntDirection*), IntBox::EMPTY(), IntOctagon::EMPTY(), Simplex::EMPTY(), Side::ON_THE_LEFT()/ON_THE_RIGHT()/COLLINEAR(), OrthogonalBoundingDirections::INSTANCE(), FortyfiveDegreeBoundingDirections::INSTANCE(), Limits::CRIT_INT_BIG() (const BigInteger&), CRIT_INT/CRIT_DOUBLE constexpr.
- union -> union_(). Array returns not covariant: cutoutFrom/divideIntoSections -> Array<TileShape*>*. PolylineArea::getHoles() new Array<Shape*>*, holeArr typed.
- FloatPoint/Circle toString(Locale) -> toString(); FloatPoint::toString(int fractionDigits,int padding).
- PlanarArea: JTS -> GEOS 3.14 **C API** (geos_c.h, reentrant *_r functions with a static GEOSContextHandle_t; linked as geos_c). Header has opaque struct PlanarAreaGeometry + ctor PlanarArea(PlanarAreaGeometry*) -> phase 2: PlanarAreaGeometry holds GEOSGeometry*; never destroyed (GEOS allocs go through global operator new? NO: geos_c uses C++ new internally -> GC'd; GEOSGeom_destroy optional). Map: union/difference/intersection/buffer(0)/isValid/UnaryUnion/GeometricShapeFactory(circle -> GEOSBuffer of point or explicit ring, check JTS default 100 pts).
- Added jrt::BigInteger::modInverse.
- .cpp written: Side.cpp, FortyfiveDegreeDirection.cpp.

## From plane/fanout/pipeline (done)
- protected/package-private -> public. Renames: BatchOptimizer::isTimedOut_, RoutingFallback_Strategy::apply_, FastPlaneConnectivityGrid::union_.
- Statics: BallEscapeOrder::MODES(), TwoLayerBga::MODES(), RoutingFallback::KNOWN(), ExitStubs::KICAD_PAD_SIZE()/TOLERANCE_COPY() (std::regex&), BatchAutorouter::BENCHMARK_PROFILE_ENABLED()/BENCHMARK_RETAIN_AUTOROUTE_DATABASE().
- Literal constants BOARD_RANK_LIMIT=30, TIGHT_SLACK=2*16 -> add static_assert in .cpp.
- Many optional<string>/optional<StopReason> (see headers).
- Instant -> int64 ms. Board snapshot type: MUST be BasicBoard* (facade's serialize()), any Array<int8_t>* snapshot use -> change to BasicBoard*.
- Listener adapters RoutingPipeline_StageListener::of(f1,f2,f3), BatchFanout_FanoutProgressListener::of(fn).
- BatchOptimizer sequentialization described in its header comment (lazy call() on calling thread).
- Inner classes with outer ptr: BatchFanout_Component_Pin, BatchOptimizer_ReadSortedRouteItems.

## INTEGRATION TODO (coordinator)
- DrillItem precalculatedMinWidth/FirstLayer/LastLayer: copy source caches in BasicBoard::copyItemListFrom (needs accessor in DrillItem.h).
- Pin.changedTo: add Pin::setChangedTo(Pin*), remap via itemMap in copyItemListFrom.
- JTS default overlay (old SnapIfNeeded) vs GEOS OverlayNG: check parity on boards with planes/zones; if drift, run Java with -Djts.overlay=ng to confirm.
- Communication::copyForBoard now creates BoardObserverAdaptor (fixed).
- RoutingFailureLog::deepCopy(itemMap) overload exists: board copy should use it (remap ItemFailureInfo.item to copied items) once itemMap is available (currently RoutingBoard copy ctor uses plain deepCopy()).
- jrt Math::toDegrees/toRadians fixed to Java 9+ constants.
