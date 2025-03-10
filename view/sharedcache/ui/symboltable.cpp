#include <progresstask.h>
#include "symboltable.h"

#include <QHeaderView>

#include "binaryninjaapi.h"

using namespace BinaryNinja;
using namespace SharedCacheAPI;

SymbolTableModel::SymbolTableModel(SymbolTableView* parent)
	: QAbstractTableModel(parent), m_parent(parent) {
}

int SymbolTableModel::rowCount(const QModelIndex& parent) const {
	Q_UNUSED(parent);
	return static_cast<int>(m_symbols.size());
}

int SymbolTableModel::columnCount(const QModelIndex& parent) const {
	Q_UNUSED(parent);
	// We have 2 columns: Address, Name
	return 2;
}

QVariant SymbolTableModel::data(const QModelIndex& index, int role) const {
	if (!index.isValid() || role != Qt::DisplayRole) {
		return QVariant();
	}

	const CacheSymbol& symbol = m_symbols.at(index.row());

	switch (index.column()) {
	case 0: // Address column
		return QString("0x%1").arg(symbol.address, 0, 16); // Display address as hexadecimal
	case 1: // Name column
		return QString::fromUtf8(symbol.name.c_str(), symbol.name.size());
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
	default:
		return QVariant();
	}
}

void SymbolTableModel::updateSymbols() {
	m_symbols = m_parent->m_symbols;
	setFilter(m_filter);
}

const CacheSymbol& SymbolTableModel::symbolAt(int row) const {
	return m_symbols.at(row);
}


void SymbolTableModel::setFilter(std::string text)
{
	beginResetModel();

	m_filter = text;
	m_symbols.clear();

	if (m_filter.empty())
	{
		m_symbols = m_parent->m_symbols;
	}
	else
	{
		m_symbols.reserve(m_parent->m_symbols.size());
		for (const auto& symbol : m_parent->m_symbols)
		{
			if (((std::string_view)symbol.name).find(m_filter) != std::string::npos)
			{
				m_symbols.push_back(symbol);
			}
		}
		m_symbols.shrink_to_fit();
	}

	endResetModel();
}


SymbolTableView::SymbolTableView(QWidget* parent)
	: m_model(new SymbolTableModel(this)) {

	// Set up the filter model
	setModel(m_model);

	// Configure view settings
	horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setSelectionMode(QAbstractItemView::SingleSelection);

	setSortingEnabled(true);
}

SymbolTableView::~SymbolTableView() {
	delete m_model;
}

void SymbolTableView::populateSymbols(BinaryView &view)
{
	if (auto controller = SharedCacheController::GetController(view)) {
		BackgroundThread::create(this)
			->thenBackground([this, controller]() {
				std::vector<CacheSymbol> newSymbols = controller->GetSymbols();
				m_symbols.swap(newSymbols);
			})
			->thenMainThread([this](){ m_model->updateSymbols(); })
			->start();
	}
}

void SymbolTableView::setFilter(const std::string& filter) {
	m_model->setFilter(filter);
}
