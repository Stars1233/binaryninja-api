#include <kernelcacheapi.h>
#include <binaryninjaapi.h>
#include "uitypes.h"
#include "viewframe.h"
#include "animation.h"
#include "uicontext.h"

#include <QTableView>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QHeaderView>
#include "filter.h"
#include <QStyledItemDelegate>

#include "ui/fontsettings.h"

#ifndef BINARYNINJA_KCTRIAGE_H
#define BINARYNINJA_KCTRIAGE_H

class AddressColorDelegate : public QStyledItemDelegate
{

public:
	explicit AddressColorDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);

		opt.palette.setColor(QPalette::Text, getThemeColor(BNThemeColor::AddressColor));
		opt.displayAlignment = Qt::AlignCenter | Qt::AlignVCenter;

		QStyledItemDelegate::paint(painter, opt, index);
	}
};


class FilterableTableView : public QTableView, public FilterTarget {
	Q_OBJECT

	bool m_filterByHiding;

public:
	explicit FilterableTableView(QWidget* parent = nullptr, bool filterByHiding = true)
		: QTableView(parent), m_filterByHiding(filterByHiding) {
		viewport()->installEventFilter(this);
		setFont(getMonospaceFont(parent));
	}

	~FilterableTableView() override = default;

	void setFilter(const std::string& filter) override {
		if (!m_filterByHiding)
		{
			emit filterTextChanged(QString::fromStdString(filter));
			return;
		}
		QString qFilter = QString::fromStdString(filter);
		for (int row = 0; row < model()->rowCount(); ++row) {
			bool match = false;
			for (int col = 0; col < model()->columnCount(); ++col) {
				QModelIndex index = model()->index(row, col);
				QString data = model()->data(index).toString();
				if (data.contains(qFilter, Qt::CaseInsensitive)) {
					match = true;
					break;
				}
			}
			setRowHidden(row, !match);
		}
	}

	void scrollToFirstItem() override {
		if (model()->rowCount() > 0) {
			scrollTo(model()->index(0, 0));
		}
	}

	void scrollToCurrentItem() override {
		QModelIndex currentIndex = selectionModel()->currentIndex();
		if (currentIndex.isValid()) {
			scrollTo(currentIndex);
		}
	}

	void selectFirstItem() override {
		if (model()->rowCount() > 0) {
			QModelIndex firstIndex = model()->index(0, 0);
			selectionModel()->select(firstIndex, QItemSelectionModel::ClearAndSelect);
		}
	}

	void activateFirstItem() override {
		if (model()->rowCount() > 0) {
			QModelIndex firstIndex = model()->index(0, 0);
			setCurrentIndex(firstIndex);
			emit activated(firstIndex);
		}
	}

	bool eventFilter(QObject* obj, QEvent* event) override {
		if (event->type() == QEvent::KeyPress) {
			auto keyEvent = dynamic_cast<QKeyEvent*>(event);
			if (keyEvent->key() == Qt::Key_Escape) {
				clearSelection();
				return true;
			}
			if (keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Return) {
				emit activated(currentIndex());
				return true;
			}
		}
		return QTableView::eventFilter(obj, event);
	}

signals:
	void filterTextChanged(const QString& text);
};

class SymbolTableView;

class SymbolTableModel : public QAbstractTableModel {
	Q_OBJECT

	SymbolTableView* m_parent;
	QFont m_font;
	std::string m_filter;
	std::vector<KernelCacheAPI::KCSymbol> m_symbols;

public:
	explicit SymbolTableModel(SymbolTableView* parent);

	int rowCount(const QModelIndex& parent) const override;
	int columnCount(const QModelIndex& parent) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
	void updateSymbols();
	void setFilter(std::string text);

	const KernelCacheAPI::KCSymbol& symbolAt(int row) const;
};


class SymbolTableView : public QTableView, public FilterTarget
{
	Q_OBJECT
	friend class SymbolTableModel;

	std::vector<KernelCacheAPI::KCSymbol> m_symbols;

	SymbolTableModel* m_model;

public:
	SymbolTableView(QWidget* parent, Ref<KernelCacheAPI::KernelCache> cache);
	~SymbolTableView() override;

	void scrollToFirstItem() override {
		if (model()->rowCount() > 0) {
			scrollTo(model()->index(0, 0));
		}
	}

	void scrollToCurrentItem() override {
		QModelIndex currentIndex = selectionModel()->currentIndex();
		if (currentIndex.isValid()) {
			scrollTo(currentIndex);
		}
	}

	void selectFirstItem() override {
		if (model()->rowCount() > 0) {
			QModelIndex firstIndex = model()->index(0, 0);
			selectionModel()->select(firstIndex, QItemSelectionModel::ClearAndSelect);
		}
	}

	void activateFirstItem() override {
		if (model()->rowCount() > 0) {
			QModelIndex firstIndex = model()->index(0, 0);
			setCurrentIndex(firstIndex);
			emit activated(firstIndex);
		}
	}

	KernelCacheAPI::KCSymbol getSymbolAtRow(int row) const
	{
		return m_model->symbolAt(row);
	}

	void setFilter(const std::string& filter) override;
};


class KCTriageView : public QWidget, public View, public UIContextNotification
{
	BinaryViewRef m_data;
	QVBoxLayout* m_layout;

	Ref<KernelCacheAPI::KernelCache> m_cache;

	SplitTabWidget* m_triageTabs;
	DockableTabCollection* m_triageCollection;

	FilterableTableView* m_imageTable;
	QStandardItemModel* m_imageModel;

	SymbolTableView* m_symbolTable;

	std::mutex m_headersMutex;
	std::shared_ptr<std::vector<KernelCacheAPI::KernelCacheMachOHeader>> m_headers;

public:
	KCTriageView(QWidget* parent, BinaryViewRef data);
	~KCTriageView() override;
	BinaryViewRef getData() override;
	void setSelectionOffsets(BNAddressRange range) override {};
	QFont getFont() override;
	bool navigate(uint64_t offset) override;
	uint64_t getCurrentOffset() override;

private:
	QWidget* initImageTable();
	void initSymbolTable();
};


class KCTriageViewType : public ViewType
{
public:
	KCTriageViewType();
	int getPriority(BinaryViewRef data, const QString& filename) override;
	QWidget* create(BinaryViewRef data, ViewFrame* viewFrame) override;
	static void Register();
};


#endif	// BINARYNINJA_KCTRIAGE_H
