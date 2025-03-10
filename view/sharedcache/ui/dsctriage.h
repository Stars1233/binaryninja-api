//
// Created by kat on 8/15/24.
//

#include <binaryninjaapi.h>
#include <QStyledItemDelegate>

#include "uitypes.h"
#include "viewframe.h"
#include "animation.h"
#include "uicontext.h"

#include "filter.h"
#include "symboltable.h"

#ifndef BINARYNINJA_DSCTRIAGE_H
#define BINARYNINJA_DSCTRIAGE_H

class FilterableTableView : public QTableView, public FilterTarget {
	Q_OBJECT

	bool m_filterByHiding;

public:
	explicit FilterableTableView(QWidget* parent = nullptr, bool filterByHiding = true)
		: QTableView(parent), m_filterByHiding(filterByHiding) {
		viewport()->installEventFilter(this);
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
			QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
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

class DSCTriageView : public QWidget, public View, public UIContextNotification
{
	BinaryViewRef m_data;
	QVBoxLayout* m_layout;

	SplitTabWidget* m_triageTabs;
	DockableTabCollection* m_triageCollection;

	QStandardItemModel* m_imageModel;

	SymbolTableView* m_symbolTable;

	QStandardItemModel* m_mappingModel;

	QStandardItemModel* m_regionModel;

public:
	DSCTriageView(QWidget* parent, BinaryViewRef data);
	~DSCTriageView() override;
	BinaryViewRef getData() override;
	void setSelectionOffsets(BNAddressRange range) override {};
	QFont getFont() override;
	bool navigate(uint64_t offset) override;
	uint64_t getCurrentOffset() override;

	void OnAfterOpenFile(UIContext* context, FileContext* file, ViewFrame* frame) override;
	void RefreshData();
	void setImageLoaded(uint64_t imageHeaderAddr);
};


class DSCTriageViewType : public ViewType
{
public:
	DSCTriageViewType();
	int getPriority(BinaryViewRef data, const QString& filename) override;
	QWidget* create(BinaryViewRef data, ViewFrame* viewFrame) override;
	static void Register();
};


#endif	// BINARYNINJA_DSCTRIAGE_H
