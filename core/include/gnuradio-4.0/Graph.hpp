#ifndef GNURADIO_GRAPH_HPP
#define GNURADIO_GRAPH_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Buffer.hpp>
#include <gnuradio-4.0/CircularBuffer.hpp>
#include <gnuradio-4.0/meta/typelist.hpp>
#include <gnuradio-4.0/Port.hpp>
#include <gnuradio-4.0/reflection.hpp>
#include <gnuradio-4.0/Sequence.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <algorithm>
#include <complex>
#include <iostream>
#include <map>
#include <ranges>
#include <tuple>
#include <variant>

#if !__has_include(<source_location> )
#define HAVE_SOURCE_LOCATION 0
#else

#include <source_location>

#if defined __cpp_lib_source_location && __cpp_lib_source_location >= 201907L
#define HAVE_SOURCE_LOCATION 1
#else
#define HAVE_SOURCE_LOCATION 0
#endif
#endif

namespace gr {

class BlockModel {
protected:
    struct NamedPortCollection {
        std::string                  name;
        std::vector<gr::DynamicPort> ports;
    };

    using DynamicPortOrCollection             = std::variant<gr::DynamicPort, NamedPortCollection>;
    using DynamicPorts                        = std::vector<DynamicPortOrCollection>;
    bool                  _dynamicPortsLoaded = false;
    std::function<void()> _dynamicPortsLoader;
    DynamicPorts          _dynamicInputPorts;
    DynamicPorts          _dynamicOutputPorts;

    BlockModel() = default;

public:
    BlockModel(const BlockModel &) = delete;
    BlockModel &
    operator=(const BlockModel &)
            = delete;
    BlockModel(BlockModel &&other) = delete;
    BlockModel &
    operator=(BlockModel &&other)
            = delete;

    void
    initDynamicPorts() const {
        if (!_dynamicPortsLoaded) _dynamicPortsLoader();
    }

    MsgPortInNamed<"__Builtin">  *msgIn;
    MsgPortOutNamed<"__Builtin"> *msgOut;

    [[nodiscard]] gr::DynamicPort &
    dynamicInputPort(std::size_t index, std::size_t subIndex = meta::invalid_index) {
        initDynamicPorts();
        if (auto *portCollection = std::get_if<NamedPortCollection>(&_dynamicInputPorts.at(index))) {
            if (subIndex == meta::invalid_index) {
                throw std::invalid_argument("Need to specify the index in the port collection");
            } else {
                return portCollection->ports[subIndex];
            }

        } else if (auto *port = std::get_if<gr::DynamicPort>(&_dynamicInputPorts.at(index))) {
            if (subIndex == meta::invalid_index) {
                return *port;
            } else {
                throw std::invalid_argument("Specified sub-index for a normal port");
            }
        }

        throw std::logic_error("Variant construction failed");
    }

    [[nodiscard]] gr::DynamicPort &
    dynamicOutputPort(std::size_t index, std::size_t subIndex = meta::invalid_index) {
        initDynamicPorts();
        if (auto *portCollection = std::get_if<NamedPortCollection>(&_dynamicOutputPorts.at(index))) {
            if (subIndex == meta::invalid_index) {
                throw std::invalid_argument("Need to specify the index in the port collection");
            } else {
                return portCollection->ports[subIndex];
            }

        } else if (auto *port = std::get_if<gr::DynamicPort>(&_dynamicOutputPorts.at(index))) {
            if (subIndex == meta::invalid_index) {
                return *port;
            } else {
                throw std::invalid_argument("Specified sub-index for a normal port");
            }
        }

        throw std::logic_error("Variant construction failed");
    }

    [[nodiscard]] std::size_t
    dynamicInputPortsSize(std::size_t parentIndex = meta::invalid_index) const {
        initDynamicPorts();
        if (parentIndex == meta::invalid_index) {
            return _dynamicInputPorts.size();
        } else {
            if (auto *portCollection = std::get_if<NamedPortCollection>(&_dynamicInputPorts.at(parentIndex))) {
                return portCollection->ports.size();
            } else {
                return meta::invalid_index;
            }
        }
    }

    [[nodiscard]] std::size_t
    dynamicOutputPortsSize(std::size_t parentIndex = meta::invalid_index) const {
        initDynamicPorts();
        if (parentIndex == meta::invalid_index) {
            return _dynamicOutputPorts.size();
        } else {
            if (auto *portCollection = std::get_if<NamedPortCollection>(&_dynamicOutputPorts.at(parentIndex))) {
                return portCollection->ports.size();
            } else {
                return meta::invalid_index;
            }
        }
    }

    std::size_t
    dynamicInputPortIndex(std::string_view name) const {
        initDynamicPorts();
        for (std::size_t i = 0; i < _dynamicInputPorts.size(); ++i) {
            if (auto *portCollection = std::get_if<NamedPortCollection>(&_dynamicInputPorts.at(i))) {
                if (portCollection->name == name) {
                    return i;
                }
            } else if (auto *port = std::get_if<gr::DynamicPort>(&_dynamicInputPorts.at(i))) {
                if (port->name == name) {
                    return i;
                }
            }
        }

        throw std::invalid_argument(fmt::format("Port {} does not exist", name));
    }

    std::size_t
    dynamicOutputPortIndex(std::string_view name) const {
        initDynamicPorts();
        for (std::size_t i = 0; i < _dynamicOutputPorts.size(); ++i) {
            if (auto *portCollection = std::get_if<NamedPortCollection>(&_dynamicOutputPorts.at(i))) {
                if (portCollection->name == name) {
                    return i;
                }
            } else if (auto *port = std::get_if<gr::DynamicPort>(&_dynamicOutputPorts.at(i))) {
                if (port->name == name) {
                    return i;
                }
            }
        }

        throw std::invalid_argument(fmt::format("Port {} does not exist", name));
    }

    virtual ~
    BlockModel()
            = default;

    /**
     * @brief to be called by scheduler->graph to initialise block
     */
    virtual void
    init(std::shared_ptr<gr::Sequence> progress, std::shared_ptr<gr::thread_pool::BasicThreadPool> ioThreadPool)
            = 0;

    /**
     * @brief returns scheduling hint that invoking the work(...) function may block on IO or system-calls
     */
    [[nodiscard]] virtual constexpr bool
    isBlocking() const noexcept
            = 0;

    /**
     * @brief change Block state (N.B. IDLE, INITIALISED, RUNNING, REQUESTED_STOP, REQUESTED_PAUSE, STOPPED, PAUSED, ERROR)
     * See enum description for details.
     */
    [[nodiscard]] virtual std::expected<void, Error>
    changeState(lifecycle::State newState) noexcept = 0;

    /**
     * @brief Block state (N.B. IDLE, INITIALISED, RUNNING, REQUESTED_STOP, REQUESTED_PAUSE, STOPPED, PAUSED, ERROR)
     * See enum description for details.
     */
    [[nodiscard]] virtual lifecycle::State
    state() const noexcept
            = 0;

    /**
     * @brief number of available readable samples at the block's input ports
     */
    [[nodiscard]] virtual constexpr std::size_t
    availableInputSamples(std::vector<std::size_t> &) const noexcept
            = 0;

    /**
     * @brief number of available writable samples at the block's output ports
     */
    [[nodiscard]] virtual constexpr std::size_t
    availableOutputSamples(std::vector<std::size_t> &) const noexcept
            = 0;

    /**
     * @brief user defined name
     */
    [[nodiscard]] virtual std::string_view
    name() const
            = 0;

    /**
     * @brief the type of the node as a string
     */
    [[nodiscard]] virtual std::string_view
    typeName() const
            = 0;

    /**
     * @brief user-defined name
     * N.B. may not be unique -> ::uniqueName
     */
    virtual void
    setName(std::string name) noexcept
            = 0;

    /**
     * @brief used to store non-graph-processing information like UI block position etc.
     */
    [[nodiscard]] virtual property_map &
    metaInformation() noexcept
            = 0;

    [[nodiscard]] virtual const property_map &
    metaInformation() const
            = 0;

    /**
     * @brief process-wide unique name
     * N.B. can be used to disambiguate in case user provided the same 'name()' for several blocks.
     */
    [[nodiscard]] virtual std::string_view
    uniqueName() const
            = 0;

    [[nodiscard]] virtual SettingsBase &
    settings() const
            = 0;

    [[nodiscard]] virtual work::Result
    work(std::size_t requested_work)
            = 0;

    [[nodiscard]] virtual work::Status
    draw() = 0;

    virtual void
    processScheduledMessages()
            = 0;

    virtual UICategory
    uiCategory() const {
        return UICategory::None;
    }

    [[nodiscard]] virtual void *
    raw() = 0;
};

namespace detail {
template<typename T, typename... Ts>
constexpr bool contains_type = (std::is_same_v<T, Ts> || ...);
}

template<BlockLike T>
    requires std::is_constructible_v<T, property_map>
class BlockWrapper : public BlockModel {
private:
    static_assert(std::is_same_v<T, std::remove_reference_t<T>>);
    T           _block;
    std::string _type_name = gr::meta::type_name<T>();

    [[nodiscard]] constexpr const auto &
    blockRef() const noexcept {
        if constexpr (requires { *_block; }) {
            return *_block;
        } else {
            return _block;
        }
    }

    [[nodiscard]] constexpr auto &
    blockRef() noexcept {
        if constexpr (requires { *_block; }) {
            return *_block;
        } else {
            return _block;
        }
    }

    void
    initMessagePorts() {
        msgIn  = std::addressof(_block.msgIn);
        msgOut = std::addressof(_block.msgOut);
    }

    template<typename TPort>
    constexpr static auto &
    processPort(auto &where, TPort &port) noexcept {
        where.push_back(gr::DynamicPort(port, DynamicPort::non_owned_reference_tag{}));
        return where.back();
    }

    void
    dynamicPortLoader() {
        if (_dynamicPortsLoaded) return;

        auto registerPort = [this]<typename Direction, typename ConstIndex, typename CurrentPortType>(DynamicPorts &where, [[maybe_unused]] Direction direction, [[maybe_unused]] ConstIndex index,
                                                                                                      CurrentPortType &&) noexcept {
            if constexpr (traits::port::is_port_v<CurrentPortType>) {
                using PortDescriptor = typename CurrentPortType::ReflDescriptor;
                if constexpr (refl::trait::is_descriptor_v<PortDescriptor>) {
                    auto &port = (blockRef().*(PortDescriptor::pointer));
                    if (port.name.empty()) {
                        port.name = refl::descriptor::get_name(PortDescriptor()).data;
                    }
                    processPort(where, port);
                } else {
                    // We can also have ports defined as template parameters
                    if constexpr (Direction::value == PortDirection::INPUT) {
                        processPort(where, gr::inputPort<ConstIndex::value, PortType::ANY>(&blockRef()));
                    } else {
                        processPort(where, gr::outputPort<ConstIndex::value, PortType::ANY>(&blockRef()));
                    }
                }
            } else {
                using PortCollectionDescriptor = typename CurrentPortType::value_type::ReflDescriptor;
                if constexpr (refl::trait::is_descriptor_v<PortCollectionDescriptor>) {
                    auto               &collection = (blockRef().*(PortCollectionDescriptor::pointer));
                    NamedPortCollection result;
                    result.name = refl::descriptor::get_name(PortCollectionDescriptor()).data;
                    for (auto &port : collection) {
                        processPort(result.ports, port);
                    }
                    where.push_back(std::move(result));
                } else {
                    static_assert(meta::always_false<PortCollectionDescriptor>, "Port collections are only supported for member variables");
                }
            }
        };

        using Node = std::remove_cvref_t<decltype(blockRef())>;
        traits::block::all_input_ports<Node>::for_each(registerPort, _dynamicInputPorts, std::integral_constant<PortDirection, PortDirection::INPUT>{});
        traits::block::all_output_ports<Node>::for_each(registerPort, _dynamicOutputPorts, std::integral_constant<PortDirection, PortDirection::OUTPUT>{});

        _dynamicPortsLoaded = true;
    }

public:
    BlockWrapper(const BlockWrapper &other) = delete;
    BlockWrapper(BlockWrapper &&other)      = delete;
    BlockWrapper &
    operator=(const BlockWrapper &other)
            = delete;
    BlockWrapper &
    operator=(BlockWrapper &&other)
            = delete;

    ~
    BlockWrapper() override
            = default;

    explicit
    BlockWrapper(property_map initParameter = {})
        : _block(std::move(initParameter)) {
        initMessagePorts();
        _dynamicPortsLoader = std::bind(&BlockWrapper::dynamicPortLoader, this);
    }

    void
    init(std::shared_ptr<gr::Sequence> progress, std::shared_ptr<gr::thread_pool::BasicThreadPool> ioThreadPool) override {
        return blockRef().init(progress, ioThreadPool);
    }

    [[nodiscard]] constexpr work::Result
    work(std::size_t requested_work = std::numeric_limits<std::size_t>::max()) override {
        return blockRef().work(requested_work);
    }

    constexpr work::Status
    draw() override {
        if constexpr (requires { blockRef().draw(); }) {
            return blockRef().draw();
        }
        return work::Status::ERROR;
    }

    UICategory
    uiCategory() const override {
        return T::DrawableControl::kCategory;
    }

    void
    processScheduledMessages() override {
        return blockRef().processScheduledMessages();
    }

    [[nodiscard]] constexpr bool
    isBlocking() const noexcept override {
        return blockRef().isBlocking();
    }

    [[nodiscard]] std::expected<void, Error>
    changeState(lifecycle::State newState) noexcept override {
        return blockRef().changeStateTo(newState);
    }

    [[nodiscard]] lifecycle::State
    state() const noexcept override {
        return blockRef().state();
    }

    [[nodiscard]] constexpr std::size_t
    availableInputSamples(std::vector<std::size_t> &data) const noexcept override {
        return blockRef().availableInputSamples(data);
    }

    [[nodiscard]] constexpr std::size_t
    availableOutputSamples(std::vector<std::size_t> &data) const noexcept override {
        return blockRef().availableOutputSamples(data);
    }

    [[nodiscard]] std::string_view
    name() const override {
        return blockRef().name;
    }

    void
    setName(std::string name) noexcept override {
        blockRef().name = std::move(name);
    }

    [[nodiscard]] std::string_view
    typeName() const override {
        return _type_name;
    }

    [[nodiscard]] property_map &
    metaInformation() noexcept override {
        return blockRef().meta_information;
    }

    [[nodiscard]] const property_map &
    metaInformation() const override {
        return blockRef().meta_information;
    }

    [[nodiscard]] std::string_view
    uniqueName() const override {
        return blockRef().unique_name;
    }

    [[nodiscard]] SettingsBase &
    settings() const override {
        return blockRef().settings();
    }

    [[nodiscard]] void *
    raw() override {
        return std::addressof(blockRef());
    }
};

template<typename T>
struct PortIndexDefinition {
    T           topLevel;
    std::size_t subIndex;

    constexpr
    PortIndexDefinition(T _topLevel, std::size_t _subIndex = meta::invalid_index)
        : topLevel(std::move(_topLevel)), subIndex(_subIndex) {}
};

class Edge {
public: // TODO: consider making this private and to use accessors (that can be safely used by users)
    using PortDirection::INPUT;
    using PortDirection::OUTPUT;
    BlockModel                      *_sourceBlock;
    BlockModel                      *_destinationBlock;
    PortIndexDefinition<std::size_t> _sourcePortDefinition;
    PortIndexDefinition<std::size_t> _destinationPortDefinition;
    std::size_t                      _minBufferSize;
    std::int32_t                     _weight;
    std::string                      _name; // custom edge name
    bool                             _connected;

public:
    Edge() = delete;

    Edge(const Edge &) = delete;

    Edge &
    operator=(const Edge &)
            = delete;

    Edge(Edge &&) noexcept = default;

    Edge &
    operator=(Edge &&) noexcept
            = default;

    Edge(BlockModel *sourceBlock, PortIndexDefinition<std::size_t> sourcePortDefinition, BlockModel *destinationBlock, PortIndexDefinition<std::size_t> destinationPortDefinition,
         std::size_t minBufferSize, std::int32_t weight, std::string_view name)
        : _sourceBlock(sourceBlock)
        , _destinationBlock(destinationBlock)
        , _sourcePortDefinition(sourcePortDefinition)
        , _destinationPortDefinition(destinationPortDefinition)
        , _minBufferSize(minBufferSize)
        , _weight(weight)
        , _name(name) {}

    [[nodiscard]] constexpr const BlockModel &
    sourceBlock() const noexcept {
        return *_sourceBlock;
    }

    [[nodiscard]] constexpr const BlockModel &
    destinationBlock() const noexcept {
        return *_destinationBlock;
    }

    [[nodiscard]] constexpr PortIndexDefinition<std::size_t>
    sourcePortDefinition() const noexcept {
        return _sourcePortDefinition;
    }

    [[nodiscard]] constexpr PortIndexDefinition<std::size_t>
    destinationPortDefinition() const noexcept {
        return _destinationPortDefinition;
    }

    [[nodiscard]] constexpr std::string_view
    name() const noexcept {
        return _name;
    }

    [[nodiscard]] constexpr std::size_t
    minBufferSize() const noexcept {
        return _minBufferSize;
    }

    [[nodiscard]] constexpr std::int32_t
    weight() const noexcept {
        return _weight;
    }

    [[nodiscard]] constexpr bool
    is_connected() const noexcept {
        return _connected;
    }
};

class Graph : public gr::Block<Graph> {
    alignas(hardware_destructive_interference_size) std::shared_ptr<gr::Sequence> progress                         = std::make_shared<gr::Sequence>();
    alignas(hardware_destructive_interference_size) std::shared_ptr<gr::thread_pool::BasicThreadPool> ioThreadPool = std::make_shared<gr::thread_pool::BasicThreadPool>(
            "graph_thread_pool", gr::thread_pool::TaskType::IO_BOUND, 2UZ, std::numeric_limits<uint32_t>::max());

private:
    std::vector<std::function<ConnectionResult(Graph &)>> _connectionDefinitions;
    std::vector<Edge>                                     _edges;

    std::vector<std::unique_ptr<BlockModel>> _blocks;

    template<typename TBlock>
    std::unique_ptr<BlockModel> &
    findBlock(TBlock &what) {
        static_assert(!std::is_pointer_v<std::remove_cvref_t<TBlock>>);
        auto it = [&, this] {
            if constexpr (std::is_same_v<TBlock, BlockModel>) {
                return std::find_if(_blocks.begin(), _blocks.end(), [&](const auto &block) { return block.get() == &what; });
            } else {
                return std::find_if(_blocks.begin(), _blocks.end(), [&](const auto &block) { return block->raw() == &what; });
            }
        }();

        if (it == _blocks.end()) throw std::runtime_error(fmt::format("No such block in this graph"));
        return *it;
    }

    template<std::size_t sourcePortIndex, std::size_t sourcePortSubIndex, std::size_t destinationPortIndex, std::size_t destinationPortSubIndex, typename Source, typename SourcePort,
             typename Destination, typename DestinationPort>
    [[nodiscard]] ConnectionResult
    connectImpl(Source &sourceNodeRaw, SourcePort &source_port_or_collection, Destination &destinationNodeRaw, DestinationPort &destinationPort_or_collection, std::size_t minBufferSize = 65536,
                std::int32_t weight = 0, std::string_view edgeName = "unnamed edge") {
        if (!std::any_of(_blocks.begin(), _blocks.end(), [&](const auto &registeredNode) { return registeredNode->raw() == std::addressof(sourceNodeRaw); })
            || !std::any_of(_blocks.begin(), _blocks.end(), [&](const auto &registeredNode) { return registeredNode->raw() == std::addressof(destinationNodeRaw); })) {
            throw std::runtime_error(
                    fmt::format("Can not connect nodes that are not registered first:\n {}:{} -> {}:{}\n", sourceNodeRaw.name, sourcePortIndex, destinationNodeRaw.name, destinationPortIndex));
        }

        auto *sourcePort = [&] {
            if constexpr (traits::port::is_port_v<SourcePort>) {
                return &source_port_or_collection;
            } else {
                return &source_port_or_collection[sourcePortSubIndex];
            }
        }();

        auto *destinationPort = [&] {
            if constexpr (traits::port::is_port_v<DestinationPort>) {
                return &destinationPort_or_collection;
            } else {
                return &destinationPort_or_collection[destinationPortSubIndex];
            }
        }();

        if constexpr (!std::is_same_v<typename std::remove_pointer_t<decltype(destinationPort)>::value_type, typename std::remove_pointer_t<decltype(sourcePort)>::value_type>) {
            meta::print_types<meta::message_type<"The source port type needs to match the sink port type">, typename std::remove_pointer_t<decltype(destinationPort)>::value_type,
                              typename std::remove_pointer_t<decltype(sourcePort)>::value_type>{};
        }

        auto result = sourcePort->connect(*destinationPort);
        if (result == ConnectionResult::SUCCESS) {
            auto *sourceNode      = findBlock(sourceNodeRaw).get();
            auto *destinationNode = findBlock(destinationNodeRaw).get();
            // TODO: Rethink edge definition, indices, message port -1 etc.
            _edges.emplace_back(sourceNode, PortIndexDefinition<std::size_t>{ sourcePortIndex, sourcePortSubIndex }, destinationNode,
                                PortIndexDefinition<std::size_t>{ destinationPortIndex, destinationPortSubIndex }, minBufferSize, weight, edgeName);
        }

        return result;
    }

    // Just a dummy class that stores the graph and the source block and port
    // to be able to split the connection into two separate calls
    // connect(source) and .to(destination)
    template<typename Source, typename Port, std::size_t sourcePortIndex = 1UZ, std::size_t sourcePortSubIndex = meta::invalid_index>
    struct SourceConnector {
        Graph  &self;
        Source &source;
        Port   &port;

        SourceConnector(Graph &_self, Source &_source, Port &_port) : self(_self), source(_source), port(_port) {}

        static_assert(std::is_same_v<Port, gr::Message> || traits::port::is_port_v<Port> || (sourcePortSubIndex != meta::invalid_index),
                      "When we have a collection of ports, we need to have an index to access the desired port in the collection");

    private:
        template<typename Destination, typename DestinationPort, std::size_t destinationPortIndex = meta::invalid_index, std::size_t destinationPortSubIndex = meta::invalid_index>
        [[nodiscard]] constexpr ConnectionResult
        to(Destination &destination, DestinationPort &destinationPort) {
            // Not overly efficient as the block doesn't know the graph it belongs to,
            // but this is not a frequent operation and the check is important.
            auto is_block_known = [this](const auto &query_block) {
                return std::any_of(self._blocks.cbegin(), self._blocks.cend(), [&query_block](const auto &known_block) { return known_block->raw() == std::addressof(query_block); });
            };
            if (!is_block_known(source) || !is_block_known(destination)) {
                fmt::print("Source {} and/or destination {} do not belong to this graph\n", source.name, destination.name);
                return ConnectionResult::FAILED;
            }
            self._connectionDefinitions.push_back([src = &source, source_port = &port, destination = &destination, destinationPort = &destinationPort](Graph &graph) {
                return graph.connectImpl<sourcePortIndex, sourcePortSubIndex, destinationPortIndex, destinationPortSubIndex>(*src, *source_port, *destination, *destinationPort);
            });
            return ConnectionResult::SUCCESS;
        }

    public:
        // connect using the port index

        template<std::size_t destinationPortIndex, std::size_t destinationPortSubIndex, typename Destination>
        [[nodiscard]] auto
        to_internal(Destination &destination) {
            auto &destinationPort = inputPort<destinationPortIndex, PortType::ANY>(&destination);
            return to<Destination, std::remove_cvref_t<decltype(destinationPort)>, destinationPortIndex, destinationPortSubIndex>(destination, destinationPort);
        }

        template<std::size_t destinationPortIndex, std::size_t destinationPortSubIndex, typename Destination>
        [[nodiscard, deprecated("For internal use only, the one with the port name should be used")]] auto
        to(Destination &destination) {
            return to_internal<destinationPortIndex, destinationPortSubIndex, Destination>(destination);
        }

        template<std::size_t destinationPortIndex, typename Destination>
        [[nodiscard]] auto
        to(Destination &destination) {
            if constexpr (destinationPortIndex == gr::meta::default_message_port_index) {
                return to<Destination, decltype(destination.msgIn)>(destination, destination.msgIn);

            } else {
                return to<destinationPortIndex, meta::invalid_index, Destination>(destination);
            }
        }

        // connect using the port name

        template<fixed_string destinationPortName, std::size_t destinationPortSubIndex, typename Destination>
        [[nodiscard]] constexpr auto
        to(Destination &destination) {
            using destination_input_ports              = typename traits::block::all_input_ports<Destination>;
            constexpr std::size_t destinationPortIndex = meta::indexForName<destinationPortName, destination_input_ports>();
            if constexpr (destinationPortIndex == meta::invalid_index) {
                meta::print_types<meta::message_type<"There is no input port with the specified name in this destination block">, Destination, meta::message_type<destinationPortName>,
                                  meta::message_type<"These are the known names:">, traits::block::all_input_port_names<Destination>, meta::message_type<"Full ports info:">, destination_input_ports>
                        port_not_found_error{};
            }
            return to_internal<destinationPortIndex, destinationPortSubIndex, Destination>(destination);
        }

        template<fixed_string destinationPortName, typename Destination>
        [[nodiscard]] constexpr auto
        to(Destination &destination) {
            return to<destinationPortName, meta::invalid_index, Destination>(destination);
        }

        SourceConnector(const SourceConnector &) = delete;
        SourceConnector(SourceConnector &&)      = delete;
        SourceConnector &
        operator=(const SourceConnector &)
                = delete;
        SourceConnector &
        operator=(SourceConnector &&)
                = delete;
    };

public:
    Graph(Graph &)  = delete;
    Graph(Graph &&) = default;
    Graph()         = default;
    Graph &
    operator=(Graph &)
            = delete;
    Graph &
    operator=(Graph &&)
            = delete;

    /**
     * @return a list of all blocks contained in this graph
     * N.B. some 'blocks' may be (sub-)graphs themselves
     */
    [[nodiscard]] std::span<std::unique_ptr<BlockModel>>
    blocks() noexcept {
        return { _blocks };
    }

    /**
     * @return a list of all edges in this graph connecting blocks
     */
    [[nodiscard]] std::span<Edge>
    edges() noexcept {
        return { _edges };
    }

    BlockModel &
    addBlock(std::unique_ptr<BlockModel> block) {
        auto &new_block_ref = _blocks.emplace_back(std::move(block));
        new_block_ref->init(progress, ioThreadPool);
        // TODO: Should we connectChildMessagePorts for these blocks as well?
        return *new_block_ref.get();
    }

    template<BlockLike TBlock, typename... Args>
        requires std::is_constructible_v<TBlock, property_map>
    auto &
    emplaceBlock(Args &&...args) { // TODO for review: do we still need this factory method or allow only pmt-map-type constructors (see below)
        static_assert(std::is_same_v<TBlock, std::remove_reference_t<TBlock>>);
        auto &new_block_ref = _blocks.emplace_back(std::make_unique<BlockWrapper<TBlock>>(std::forward<Args>(args)...));
        auto  raw_ref       = static_cast<TBlock *>(new_block_ref->raw());
        raw_ref->init(progress, ioThreadPool);
        return *raw_ref;
    }

    template<BlockLike TBlock>
        requires std::is_constructible_v<TBlock, property_map>
    auto &
    emplaceBlock(property_map initialSettings) {
        static_assert(std::is_same_v<TBlock, std::remove_reference_t<TBlock>>);
        auto      &new_block_ref = _blocks.emplace_back(std::make_unique<BlockWrapper<TBlock>>(std::move(initialSettings)));
        auto       raw_ref       = static_cast<TBlock *>(new_block_ref->raw());
        const auto failed        = raw_ref->settings().set(initialSettings);
        raw_ref->init(progress, ioThreadPool);
        return *raw_ref;
    }

    // connect using the port index

    template<std::size_t sourcePortIndex, std::size_t sourcePortSubIndex, typename Source>
    [[nodiscard]] auto
    connect_internal(Source &source) {
        auto &port_or_collection = outputPort<sourcePortIndex, PortType::ANY>(&source);
        return SourceConnector<Source, std::remove_cvref_t<decltype(port_or_collection)>, sourcePortIndex, sourcePortSubIndex>(*this, source, port_or_collection);
    }

    template<std::size_t sourcePortIndex, std::size_t sourcePortSubIndex, typename Source>
    [[nodiscard, deprecated("The connect with the port name should be used")]] auto
    connect(Source &source) {
        return connect_internal<sourcePortIndex, sourcePortSubIndex, Source>(source);
    }

    template<std::size_t sourcePortIndex, typename Source>
    [[nodiscard]] auto
    connect(Source &source) {
        if constexpr (sourcePortIndex == meta::default_message_port_index) {
            return SourceConnector<Source, decltype(source.msgOut), meta::invalid_index, meta::invalid_index>(*this, source, source.msgOut);
        } else {
            return connect<sourcePortIndex, meta::invalid_index, Source>(source);
        }
    }

    // connect using the port name

    template<fixed_string sourcePortName, std::size_t sourcePortSubIndex, typename Source>
    [[nodiscard]] auto
    connect(Source &source) {
        using source_output_ports             = typename traits::block::all_output_ports<Source>;
        constexpr std::size_t sourcePortIndex = meta::indexForName<sourcePortName, source_output_ports>();
        if constexpr (sourcePortIndex == meta::invalid_index) {
            meta::print_types<meta::message_type<"There is no output port with the specified name in this source block">, Source, meta::message_type<sourcePortName>,
                              meta::message_type<"These are the known names:">, traits::block::all_output_port_names<Source>, meta::message_type<"Full ports info:">, source_output_ports>
                    port_not_found_error{};
        }
        return connect_internal<sourcePortIndex, sourcePortSubIndex, Source>(source);
    }

    template<fixed_string sourcePortName, typename Source>
    [[nodiscard]] auto
    connect(Source &source) {
        return connect<sourcePortName, meta::invalid_index, Source>(source);
    }

    // dynamic/runtime connections

    template<typename Source, typename Destination>
        requires(!std::is_pointer_v<std::remove_cvref_t<Source>> && !std::is_pointer_v<std::remove_cvref_t<Destination>>)
    ConnectionResult
    connect(Source &sourceBlockRaw, PortIndexDefinition<std::size_t> sourcePortDefinition, Destination &destinationBlockRaw, PortIndexDefinition<std::size_t> destinationPortDefinition,
            std::size_t minBufferSize = 65536, std::int32_t weight = 0, std::string_view edgeName = "unnamed edge") {
        auto result = findBlock(sourceBlockRaw)
                              ->dynamicOutputPort(sourcePortDefinition.topLevel, sourcePortDefinition.subIndex)
                              .connect(findBlock(destinationBlockRaw)->dynamicInputPort(destinationPortDefinition.topLevel, destinationPortDefinition.subIndex));
        if (result == ConnectionResult::SUCCESS) {
            auto *sourceBlock      = findBlock(sourceBlockRaw).get();
            auto *destinationBlock = findBlock(destinationBlockRaw).get();
            _edges.emplace_back(sourceBlock, sourcePortDefinition, destinationBlock, destinationPortDefinition, minBufferSize, weight, edgeName);
        }
        return result;
    }

    template<typename Source, typename Destination>
        requires(!std::is_pointer_v<std::remove_cvref_t<Source>> && !std::is_pointer_v<std::remove_cvref_t<Destination>>)
    ConnectionResult
    connect(Source &sourceBlockRaw, PortIndexDefinition<std::string> sourcePortDefinition, Destination &destinationBlockRaw, PortIndexDefinition<std::string> destinationPortDefinition,
            std::size_t minBufferSize = 65536, std::int32_t weight = 0, std::string_view edgeName = "unnamed edge") {
        auto sourcePortIndex      = this->findBlock(sourceBlockRaw)->dynamicOutputPortIndex(sourcePortDefinition.topLevel);
        auto destinationPortIndex = this->findBlock(destinationBlockRaw)->dynamicInputPortIndex(destinationPortDefinition.topLevel);
        return connect(sourceBlockRaw, { sourcePortIndex, sourcePortDefinition.subIndex }, destinationBlockRaw, { destinationPortIndex, destinationPortDefinition.subIndex }, minBufferSize, weight,
                       edgeName);
    }

    template<typename Anything>
    void
    processMessages(MsgPortInNamed<"__FromChildren"> & /*port*/, std::span<const Anything> /*input*/) {
        static_assert(meta::always_false<Anything>, "This is not called, children are processed in processScheduledMessages");
    }

    bool
    performConnections() {
        auto result = std::all_of(_connectionDefinitions.begin(), _connectionDefinitions.end(),
                                  [this](auto &connection_definition) { return connection_definition(*this) == ConnectionResult::SUCCESS; });
        if (result) {
            _connectionDefinitions.clear();
        }
        return result;
    }

    template<typename F> // TODO: F must be constraint by a descriptive concept
    void
    forEachBlock(F &&f) const {
        std::ranges::for_each(_blocks, [f](const auto &block_ptr) { std::invoke(f, *block_ptr.get()); });
    }

    template<typename F> // TODO: F must be constraint by a descriptive concept
    void
    forEachEdge(F &&f) const {
        std::ranges::for_each(_edges, [f](const auto &edge) { std::invoke(f, edge); });
    }
};

static_assert(BlockLike<Graph>);

/*******************************************************************************************************/
/**************************** begin of SIMD-Merged Graph Implementation ********************************/
/*******************************************************************************************************/

/**
 * Concepts and class for Merging Blocks to Sub-Graph Functionality
 *
 * This code provides a way to merge blocks of processing units in a flow-graph for efficient computation.
 * The merging occurs at compile-time, enabling the execution performance to be much better than running
 * each constituent block individually.
 *
 * Concepts:
 *  - `SourceBlockLike`: Represents a source block with processing capability and at least one output port.
 *  - `SinkBlockLike`: Represents a sink block with processing capability and at least one input port.
 *
 * Key Features:
 *  - `MergedGraph` class: Combines a source and sink block into a new unit, connecting them via specified
 *    output and input port indices.
 *  - The merged block can be efficiently optimized at compile-time.
 *  - Each `MergedGraph` instance has a unique ID and name, aiding in debugging and identification.
 *  - The merging works seamlessly for blocks that have single or multiple output ports.
 *  - It allows for SIMD optimizations if the constituent blocks support it.
 *
 * Utility Functions:
 *  - `mergeByIndex()`: A utility function to merge two blocks based on specified port indices.
 *    It checks if the output port of the source block and the input port of the sink block have matching types.
 *
 * Examples:
 *  This enables you to create a flow-graph where you merge blocks to create optimized processing paths.
 *  Example usage can be found in the documentation of `mergeByIndex()`.
 *
 * Dependencies:
 *  - Relies on various traits and meta-programming utilities for type introspection and compile-time checks.
 *
 * Note:
 *  - The implementation of the actual processing logic (e.g., `processOne()`, `processOne_simd()`, etc.)
 *    and their SIMD variants is specific to the logic and capabilities of the blocks being merged.
 *
 * Limitations:
 *  - Currently, SIMD support for multiple output ports is not implemented.
 */

template<typename TBlock>
concept SourceBlockLike = traits::block::can_processOne<TBlock> and traits::block::template stream_output_port_types<TBlock>::size > 0;

static_assert(not SourceBlockLike<int>);

template<typename TBlock>
concept SinkBlockLike = traits::block::can_processOne<TBlock> and traits::block::template stream_input_port_types<TBlock>::size > 0;

static_assert(not SinkBlockLike<int>);

template<SourceBlockLike Left, SinkBlockLike Right, std::size_t OutId, std::size_t InId>
class MergedGraph
    : public Block<MergedGraph<Left, Right, OutId, InId>, meta::concat<typename traits::block::stream_input_ports<Left>, meta::remove_at<InId, typename traits::block::stream_input_ports<Right>>>,
                   meta::concat<meta::remove_at<OutId, typename traits::block::stream_output_ports<Left>>, typename traits::block::stream_output_ports<Right>>> {
    static std::atomic_size_t _unique_id_counter;

public:
    const std::size_t unique_id   = _unique_id_counter++;
    const std::string unique_name = fmt::format("MergedGraph<{}:{},{}:{}>#{}", gr::meta::type_name<Left>(), OutId, gr::meta::type_name<Right>(), InId, unique_id);

private:
    // copy-paste from above, keep in sync
    using base = Block<MergedGraph<Left, Right, OutId, InId>, meta::concat<typename traits::block::stream_input_ports<Left>, meta::remove_at<InId, typename traits::block::stream_input_ports<Right>>>,
                       meta::concat<meta::remove_at<OutId, typename traits::block::stream_output_ports<Left>>, typename traits::block::stream_output_ports<Right>>>;

    Left  left;
    Right right;

    // merged_work_chunk_size, that's what friends are for
    friend base;

    template<SourceBlockLike, SinkBlockLike, std::size_t, std::size_t>
    friend class MergedGraph;

    // returns the minimum of all internal max_samples port template parameters
    static constexpr std::size_t
    merged_work_chunk_size() noexcept {
        constexpr std::size_t left_size = []() {
            if constexpr (requires {
                              { Left::merged_work_chunk_size() } -> std::same_as<std::size_t>;
                          }) {
                return Left::merged_work_chunk_size();
            } else {
                return std::dynamic_extent;
            }
        }();
        constexpr std::size_t right_size = []() {
            if constexpr (requires {
                              { Right::merged_work_chunk_size() } -> std::same_as<std::size_t>;
                          }) {
                return Right::merged_work_chunk_size();
            } else {
                return std::dynamic_extent;
            }
        }();
        return std::min({ traits::block::stream_input_ports<Right>::template apply<traits::port::max_samples>::value,
                          traits::block::stream_output_ports<Left>::template apply<traits::port::max_samples>::value, left_size, right_size });
    }

    template<std::size_t I>
    constexpr auto
    apply_left(std::size_t offset, auto &&input_tuple) noexcept {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return invokeProcessOneWithOrWithoutOffset(left, offset, std::get<Is>(std::forward<decltype(input_tuple)>(input_tuple))...);
        }(std::make_index_sequence<I>());
    }

    template<std::size_t I, std::size_t J>
    constexpr auto
    apply_right(std::size_t offset, auto &&input_tuple, auto &&tmp) noexcept {
        return [&]<std::size_t... Is, std::size_t... Js>(std::index_sequence<Is...>, std::index_sequence<Js...>) {
            constexpr std::size_t first_offset  = traits::block::stream_input_port_types<Left>::size;
            constexpr std::size_t second_offset = traits::block::stream_input_port_types<Left>::size + sizeof...(Is);
            static_assert(second_offset + sizeof...(Js) == std::tuple_size_v<std::remove_cvref_t<decltype(input_tuple)>>);
            return invokeProcessOneWithOrWithoutOffset(right, offset, std::get<first_offset + Is>(std::forward<decltype(input_tuple)>(input_tuple))..., std::forward<decltype(tmp)>(tmp),
                                                       std::get<second_offset + Js>(input_tuple)...);
        }(std::make_index_sequence<I>(), std::make_index_sequence<J>());
    }

public:
    using TInputPortTypes  = typename traits::block::stream_input_port_types<base>;
    using TOutputPortTypes = typename traits::block::stream_output_port_types<base>;
    using TReturnType      = typename traits::block::stream_return_type<base>;

    constexpr
    MergedGraph(Left l, Right r)
        : left(std::move(l)), right(std::move(r)) {}

    // if the left block (source) implements available_samples (a customization point), then pass the call through
    friend constexpr std::size_t
    available_samples(const MergedGraph &self) noexcept
        requires requires(const Left &l) {
            { available_samples(l) } -> std::same_as<std::size_t>;
        }
    {
        return available_samples(self.left);
    }

    template<meta::any_simd... Ts>
        requires traits::block::can_processOne_simd<Left> and traits::block::can_processOne_simd<Right>
    constexpr meta::simdize<TReturnType, meta::simdize_size_v<std::tuple<Ts...>>>
    processOne(std::size_t offset, const Ts &...inputs) {
        static_assert(traits::block::stream_output_port_types<Left>::size == 1, "TODO: SIMD for multiple output ports not implemented yet");
        return apply_right<InId, traits::block::stream_input_port_types<Right>::size() - InId - 1>(offset, std::tie(inputs...),
                                                                                                   apply_left<traits::block::stream_input_port_types<Left>::size()>(offset, std::tie(inputs...)));
    }

    constexpr auto
    processOne_simd(std::size_t offset, auto N)
        requires traits::block::can_processOne_simd<Right>
    {
        if constexpr (requires(Left &l) {
                          { l.processOne_simd(offset, N) };
                      }) {
            return invokeProcessOneWithOrWithoutOffset(right, offset, left.processOne_simd(offset, N));
        } else if constexpr (requires(Left &l) {
                                 { l.processOne_simd(N) };
                             }) {
            return invokeProcessOneWithOrWithoutOffset(right, offset, left.processOne_simd(N));
        } else {
            using LeftResult = typename traits::block::stream_return_type<Left>;
            using V          = meta::simdize<LeftResult, N>;
            alignas(stdx::memory_alignment_v<V>) LeftResult tmp[V::size()];
            for (std::size_t i = 0UZ; i < V::size(); ++i) {
                tmp[i] = invokeProcessOneWithOrWithoutOffset(left, offset + i);
            }
            return invokeProcessOneWithOrWithoutOffset(right, offset, V(tmp, stdx::vector_aligned));
        }
    }

    template<typename... Ts>
    // Nicer error messages for the following would be good, but not at the expense of breaking can_processOne_simd.
        requires(TInputPortTypes::template are_equal<std::remove_cvref_t<Ts>...>)
    constexpr TReturnType
    processOne(std::size_t offset, Ts &&...inputs) {
        // if (sizeof...(Ts) == 0) we could call `return processOne_simd(integral_constant<size_t, width>)`. But if
        // the caller expects to process *one* sample (no inputs for the caller to explicitly
        // request simd), and we process more, we risk inconsistencies.
        if constexpr (traits::block::stream_output_port_types<Left>::size == 1) {
            // only the result from the right block needs to be returned
            return apply_right<InId, traits::block::stream_input_port_types<Right>::size() - InId
                                             - 1>(offset, std::forward_as_tuple(std::forward<Ts>(inputs)...),
                                                  apply_left<traits::block::stream_input_port_types<Left>::size()>(offset, std::forward_as_tuple(std::forward<Ts>(inputs)...)));

        } else {
            // left produces a tuple
            auto left_out  = apply_left<traits::block::stream_input_port_types<Left>::size()>(offset, std::forward_as_tuple(std::forward<Ts>(inputs)...));
            auto right_out = apply_right<InId, traits::block::stream_input_port_types<Right>::size() - InId - 1>(offset, std::forward_as_tuple(std::forward<Ts>(inputs)...),
                                                                                                                 std::move(std::get<OutId>(left_out)));

            if constexpr (traits::block::stream_output_port_types<Left>::size == 2 && traits::block::stream_output_port_types<Right>::size == 1) {
                return std::make_tuple(std::move(std::get<OutId ^ 1>(left_out)), std::move(right_out));

            } else if constexpr (traits::block::stream_output_port_types<Left>::size == 2) {
                return std::tuple_cat(std::make_tuple(std::move(std::get<OutId ^ 1>(left_out))), std::move(right_out));

            } else if constexpr (traits::block::stream_output_port_types<Right>::size == 1) {
                return [&]<std::size_t... Is, std::size_t... Js>(std::index_sequence<Is...>, std::index_sequence<Js...>) {
                    return std::make_tuple(std::move(std::get<Is>(left_out))..., std::move(std::get<OutId + 1 + Js>(left_out))..., std::move(right_out));
                }(std::make_index_sequence<OutId>(), std::make_index_sequence<traits::block::stream_output_port_types<Left>::size - OutId - 1>());

            } else {
                return [&]<std::size_t... Is, std::size_t... Js, std::size_t... Ks>(std::index_sequence<Is...>, std::index_sequence<Js...>, std::index_sequence<Ks...>) {
                    return std::make_tuple(std::move(std::get<Is>(left_out))..., std::move(std::get<OutId + 1 + Js>(left_out))..., std::move(std::get<Ks>(right_out)...));
                }(std::make_index_sequence<OutId>(), std::make_index_sequence<traits::block::stream_output_port_types<Left>::size - OutId - 1>(),
                       std::make_index_sequence<Right::output_port_types::size>());
            }
        }
    } // end:: processOne

    // work::Result // TODO: ask Matthias if this is still needed or whether this can be simplified.
    // work(std::size_t requested_work) noexcept {
    //     return base::work(requested_work);
    // }
};

template<SourceBlockLike Left, SinkBlockLike Right, std::size_t OutId, std::size_t InId>
inline std::atomic_size_t MergedGraph<Left, Right, OutId, InId>::_unique_id_counter{ 0UZ };

/**
 * This methods can merge simple blocks that are defined via a single `auto processOne(..)` producing a
 * new `merged` node, bypassing the dynamic run-time buffers.
 * Since the merged node can be highly optimised during compile-time, it's execution performance is usually orders
 * of magnitude more efficient than executing a cascade of the same constituent blocks. See the benchmarks for details.
 * This function uses the connect-by-port-ID API.
 *
 * Example:
 * @code
 * // declare flow-graph: 2 x in -> adder -> scale-by-2 -> scale-by-minus1 -> output
 * auto merged = merge_by_index<0, 0>(scale<int, -1>(), merge_by_index<0, 0>(scale<int, 2>(), adder<int>()));
 *
 * // execute graph
 * std::array<int, 4> a = { 1, 2, 3, 4 };
 * std::array<int, 4> b = { 10, 10, 10, 10 };
 *
 * int                r = 0;
 * for (std::size_t i = 0; i < 4; ++i) {
 *     r += merged.processOne(a[i], b[i]);
 * }
 * @endcode
 */
template<std::size_t OutId, std::size_t InId, SourceBlockLike A, SinkBlockLike B>
constexpr auto
mergeByIndex(A &&a, B &&b) -> MergedGraph<std::remove_cvref_t<A>, std::remove_cvref_t<B>, OutId, InId> {
    if constexpr (!std::is_same_v<typename traits::block::stream_output_port_types<std::remove_cvref_t<A>>::template at<OutId>,
                                  typename traits::block::stream_input_port_types<std::remove_cvref_t<B>>::template at<InId>>) {
        gr::meta::print_types<gr::meta::message_type<"OUTPUT_PORTS_ARE:">, typename traits::block::stream_output_port_types<std::remove_cvref_t<A>>, std::integral_constant<int, OutId>,
                              typename traits::block::stream_output_port_types<std::remove_cvref_t<A>>::template at<OutId>,

                              gr::meta::message_type<"INPUT_PORTS_ARE:">, typename traits::block::stream_input_port_types<std::remove_cvref_t<A>>, std::integral_constant<int, InId>,
                              typename traits::block::stream_input_port_types<std::remove_cvref_t<A>>::template at<InId>>{};
    }
    return { std::forward<A>(a), std::forward<B>(b) };
}

/**
 * This methods can merge simple blocks that are defined via a single `auto processOne(..)` producing a
 * new `merged` node, bypassing the dynamic run-time buffers.
 * Since the merged node can be highly optimised during compile-time, it's execution performance is usually orders
 * of magnitude more efficient than executing a cascade of the same constituent blocks. See the benchmarks for details.
 * This function uses the connect-by-port-name API.
 *
 * Example:
 * @code
 * // declare flow-graph: 2 x in -> adder -> scale-by-2 -> output
 * auto merged = merge<"scaled", "addend1">(scale<int, 2>(), adder<int>());
 *
 * // execute graph
 * std::array<int, 4> a = { 1, 2, 3, 4 };
 * std::array<int, 4> b = { 10, 10, 10, 10 };
 *
 * int                r = 0;
 * for (std::size_t i = 0; i < 4; ++i) {
 *     r += merged.processOne(a[i], b[i]);
 * }
 * @endcode
 */
template<fixed_string OutName, fixed_string InName, SourceBlockLike A, SinkBlockLike B>
constexpr auto
merge(A &&a, B &&b) {
    constexpr int OutIdUnchecked = meta::indexForName<OutName, typename traits::block::stream_output_ports<A>>();
    constexpr int InIdUnchecked  = meta::indexForName<InName, typename traits::block::stream_input_ports<B>>();
    static_assert(OutIdUnchecked != -1);
    static_assert(InIdUnchecked != -1);
    constexpr auto OutId = static_cast<std::size_t>(OutIdUnchecked);
    constexpr auto InId  = static_cast<std::size_t>(InIdUnchecked);
    static_assert(std::same_as<typename traits::block::stream_output_port_types<std::remove_cvref_t<A>>::template at<OutId>,
                               typename traits::block::stream_input_port_types<std::remove_cvref_t<B>>::template at<InId>>,
                  "Port types do not match");
    return MergedGraph<std::remove_cvref_t<A>, std::remove_cvref_t<B>, OutId, InId>{ std::forward<A>(a), std::forward<B>(b) };
}

/*******************************************************************************************************/
/**************************** end of SIMD-Merged Graph Implementation **********************************/
/*******************************************************************************************************/

// TODO: add nicer enum formatter
inline std::ostream &
operator<<(std::ostream &os, const ConnectionResult &value) {
    return os << static_cast<int>(value);
}

inline std::ostream &
operator<<(std::ostream &os, const PortType &value) {
    return os << static_cast<int>(value);
}

inline std::ostream &
operator<<(std::ostream &os, const PortDirection &value) {
    return os << static_cast<int>(value);
}

template<PortDomainLike T>
inline std::ostream &
operator<<(std::ostream &os, const T &value) {
    return os << value.Name;
}

#if HAVE_SOURCE_LOCATION
inline auto
this_source_location(std::source_location l = std::source_location::current()) {
    return fmt::format("{}:{},{}", l.file_name(), l.line(), l.column());
}
#else
inline auto
this_source_location() {
    return "not yet implemented";
}
#endif // HAVE_SOURCE_LOCATION

} // namespace gr

REFL_TYPE(gr::Graph) REFL_END // minimal reflection declaration

#endif // include guard
