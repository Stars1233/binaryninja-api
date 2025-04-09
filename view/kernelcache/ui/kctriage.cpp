#include "globalarea.h"
#include "kctriage.h"
#include "progresstask.h"
#include "ui/fontsettings.h"
#include <QMessageBox>
#include <QPainter>
#include <cmath>

using namespace BinaryNinja;
using namespace KernelCacheAPI;


SymbolTableModel::SymbolTableModel(SymbolTableView* parent)
	: QAbstractTableModel(parent), m_parent(parent) {
	// TODO: Need to implement updating this font if it is changed by the user
	m_font = getMonospaceFont(parent);
}


int SymbolTableModel::rowCount(const QModelIndex& parent) const {
	Q_UNUSED(parent);
	return static_cast<int>(m_symbols.size());
}


int SymbolTableModel::columnCount(const QModelIndex& parent) const {
	Q_UNUSED(parent);
	// We have 3 columns: Address, Name, and Image
	return 3;
}


QVariant SymbolTableModel::data(const QModelIndex& index, int role) const {
	if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::FontRole)) {
		return QVariant();
	}

	const KCSymbol& symbol = m_symbols.at(index.row());

	switch (role)
	{
	case Qt::DisplayRole:
	{
		switch (index.column()) {
		case 0: // Address column
			return QString("0x%1").arg(symbol.address, 0, 16); // Display address as hexadecimal
		case 1: // Name column
			return QString::fromStdString(symbol.name);
		case 2: // Image column
			return QString::fromStdString(symbol.image);
		default:
			return QVariant();
		}
	}
	case Qt::FontRole:
		return m_font;
	default:
		return QVariant();
	}
}


QVariant SymbolTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
	if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
		return QVariant();
	}

	switch (section) {
	case 0:
		return QString("Address");
	case 1:
		return QString("Name");
	case 2:
		return QString("Image");
	default:
		return QVariant();
	}
}


void SymbolTableModel::updateSymbols() {
	m_symbols = m_parent->m_symbols;
	setFilter(m_filter);
}


const KCSymbol& SymbolTableModel::symbolAt(int row) const {
	return m_symbols.at(row);
}


void SymbolTableModel::setFilter(std::string text)
{
	beginResetModel();

	m_filter = text;
	m_symbols.clear();

	if (!m_filter.empty())
	{
		m_symbols.reserve(m_parent->m_symbols.size());
		for (const auto& symbol : m_parent->m_symbols)
			if (symbol.name.find(m_filter) != std::string::npos)
				m_symbols.push_back(symbol);
		m_symbols.shrink_to_fit();
	}
	else
	{
		m_symbols = m_parent->m_symbols;
	}

	endResetModel();
}


SymbolTableView::SymbolTableView(QWidget* parent, Ref<KernelCache> cache)
	: QTableView(parent), m_model(new SymbolTableModel(this)) {

	// Set up the filter model
	setModel(m_model);

	// Configure view settings
	horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	setEditTriggers(QAbstractItemView::NoEditTriggers);
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setSelectionMode(QAbstractItemView::SingleSelection);

	setSortingEnabled(true);

	BackgroundThread::create(this)->thenBackground([this, cache](){
		// LogInfo("Symbol Search: Loading symbols...");
		m_symbols = cache->LoadAllSymbolsAndWait();
		// LogInfo("Symbol Search: Loaded 0x%zx symbols", m_symbols.size());
	})->thenMainThread([this](){
		m_model->updateSymbols();
	})->start();
}


SymbolTableView::~SymbolTableView() {
	delete m_model;
}


void SymbolTableView::setFilter(const std::string& filter) {
	m_model->setFilter(filter);
}


KCTriageViewType::KCTriageViewType()
	: ViewType("KCTriage", "Kernel Cache Triage")
{}


int KCTriageViewType::getPriority(BinaryViewRef data, const QString& filename)
{
	if (data->GetTypeName() == KC_VIEW_NAME)
		return 100;
	return 0;
}


QWidget* KCTriageViewType::create(BinaryViewRef data, ViewFrame* viewFrame)
{
	if (data->GetTypeName() != KC_VIEW_NAME)
		return nullptr;
	return new KCTriageView(viewFrame, data);
}


void KCTriageViewType::Register()
{
	registerViewType(new KCTriageViewType());
}


KCTriageView::KCTriageView(QWidget* parent, BinaryViewRef data) : QWidget(parent), View(), m_data(data), m_cache(new KernelCache(data))
{
	setBinaryDataNavigable(true);
	setupView(this);

	UIContext::registerNotification(this);

	m_triageCollection = new DockableTabCollection();
	m_triageTabs = new SplitTabWidget(m_triageCollection);

	auto triageTabStyle = new GlobalAreaTabStyle();
	m_triageTabs->setTabStyle(triageTabStyle);

	QWidget* defaultWidget = initImageTable();
	initSymbolTable();

	m_layout = new QVBoxLayout(this);
	m_layout->addWidget(m_triageTabs);
	setLayout(m_layout);

	// XXX: RefreshData

	m_triageTabs->selectWidget(defaultWidget);
}


KCTriageView::~KCTriageView()
{
	UIContext::unregisterNotification(this);
}


QWidget* KCTriageView::initImageTable()
{
	m_imageTable = new FilterableTableView(this);

	m_imageModel = new QStandardItemModel(0, 2, m_imageTable);
	m_imageModel->setHorizontalHeaderLabels({"VM Address", "Name"});

	// Apply custom column styling
	m_imageTable->setItemDelegateForColumn(0, new AddressColorDelegate(m_imageTable));

	BackgroundThread::create(m_imageTable)->thenBackground([this](const QVariant var) {
		QVariantList rows;

		auto images = m_cache->GetImages();

		auto newHeaders = std::make_shared<std::vector<KernelCacheMachOHeader>>();
		newHeaders->reserve(images.size());

		for (const auto& img : images)
		{
			if (auto header = m_cache->GetMachOHeaderForImage(img.name); header)
			{
				newHeaders->push_back(*header);
				rows.push_back(QList<QVariant>{
					QString("0x%1").arg(header->textBase, 0, 16),
					QString::fromStdString(img.name)
				});
			}
		}

		std::unique_lock<std::mutex> lock(m_headersMutex);
		m_headers.swap(newHeaders);

		return QVariant(rows);
	})->thenMainThread([this](const QVariant var) {
		QVariantList rows = var.toList();

		if (m_imageModel->rowCount() > 0)
			m_imageModel->removeRows(0, m_imageModel->rowCount());

		for (const QVariant &rowVariant : rows) {
			QVariantList row = rowVariant.toList();

			QList<QStandardItem*> items;
			for (const QVariant &cellValue : row)
				items.append(new QStandardItem(cellValue.toString()));

			m_imageModel->appendRow(items);
			m_imageTable->resizeColumnsToContents();
		}
	})->start();

	auto loadImageButton = new QPushButton();
	connect(loadImageButton, &QPushButton::clicked, [this](bool) {
		// Collect only visible selected rows
		QModelIndexList visibleSelectedRows;
		for (const auto& index : m_imageTable->selectionModel()->selectedRows()) {
			if (!m_imageTable->isRowHidden(index.row())) {
				visibleSelectedRows.append(index);
			}
		}

		if (visibleSelectedRows.empty())
			return;

		for (const auto& selection : visibleSelectedRows) {
			auto name = selection.data().toString().toStdString();
			WorkerPriorityEnqueue([this, name]() { m_cache->LoadImageWithInstallName(name); });
		}
	});
	loadImageButton->setText("Load Selected");

	auto loadImageFilterEdit = new FilterEdit(m_imageTable);
	connect(loadImageFilterEdit, &FilterEdit::textChanged, [this](const QString& filter) {
		m_imageTable->setFilter(filter.toStdString());
	});

	connect(m_imageTable, &FilterableTableView::activated, this, [=](const QModelIndex& index) {
		auto selected = m_imageModel->item(index.row(), 0);
		auto name = selected->text().toStdString();
		WorkerPriorityEnqueue([this, name]() { m_cache->LoadImageWithInstallName(name); });
	});

	auto loadImageLayout = new QVBoxLayout;
	loadImageLayout->addWidget(loadImageFilterEdit);
	loadImageLayout->addWidget(m_imageTable);
	loadImageLayout->addWidget(loadImageButton);

	auto buttonLayout = new QHBoxLayout;
	buttonLayout->addWidget(loadImageButton);
	buttonLayout->setAlignment(Qt::AlignLeft);
	loadImageLayout->addLayout(buttonLayout);

	auto loadImageWidget = new QWidget;
	loadImageWidget->setLayout(loadImageLayout);

	m_imageTable->setModel(m_imageModel);

	m_imageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

	m_imageTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
	m_imageTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

	m_imageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_imageTable->setSelectionMode(QAbstractItemView::ExtendedSelection);

	m_imageTable->setSortingEnabled(true);

	m_imageTable->verticalHeader()->setVisible(false);

	m_triageTabs->addTab(loadImageWidget, "Images");
	m_triageTabs->setCanCloseTab(loadImageWidget, false);

	return loadImageWidget; // For use as the default widget
}


void KCTriageView::initSymbolTable()
{
	m_symbolTable = new SymbolTableView(this, m_cache);

	auto symbolFilterEdit = new FilterEdit(m_symbolTable);
	connect(symbolFilterEdit, &FilterEdit::textChanged, [this](const QString& filter) {
		m_symbolTable->setFilter(filter.toStdString());
	});

	// Apply custom column styling
	m_symbolTable->setItemDelegateForColumn(0, new AddressColorDelegate(m_symbolTable));

	auto symbolLayout = new QVBoxLayout;
	symbolLayout->addWidget(symbolFilterEdit);
	symbolLayout->addWidget(m_symbolTable);

	auto symbolWidget = new QWidget;
	symbolWidget->setLayout(symbolLayout);

	m_symbolTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents); // Address
	m_symbolTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);          // Name
	m_symbolTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);          // Image

	m_symbolTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_symbolTable->setSelectionMode(QAbstractItemView::SingleSelection);

	std::function<void(uint64_t)> navigateToAddress = [=](uint64_t addr) {
		ExecuteOnMainThread([addr, this](){
			if (Settings::Instance()->Get<bool>("ui.view.graph.preferred"))
				m_data->Navigate("Graph:KCView", addr);
			else
				m_data->Navigate("Linear:KCView", addr);
		});
	};

	connect(m_symbolTable, &SymbolTableView::activated, this, [=](const QModelIndex& index)
	{
		auto symbol = m_symbolTable->getSymbolAtRow(index.row());
		WorkerPriorityEnqueue([this, symbol, navigateToAddress]() {
			if (m_data->IsValidOffset(symbol.address))
				navigateToAddress(symbol.address);
			else
			{
				m_cache->LoadImageWithInstallName(symbol.image);
				navigateToAddress(symbol.address);
			}
		});
	});

	m_triageTabs->addTab(symbolWidget, "Symbols");
	m_triageTabs->setCanCloseTab(symbolWidget, false);
}


QFont KCTriageView::getFont()
{
	return getMonospaceFont(this);
}


BinaryViewRef KCTriageView::getData()
{
	return m_data;
}


bool KCTriageView::navigate(uint64_t offset)
{
	// TODO: We have to set this to true otherwise view restore does not pickup this view.
	return true;
}


uint64_t KCTriageView::getCurrentOffset()
{
	return 0;
}
