#pragma once

#include <sharedcacheapi.h>
#include "viewframe.h"
#include "animation.h"

#include <QTableView>
#include <QStandardItemModel>
#include "filter.h"

class SymbolTableView;

class SymbolTableModel : public QAbstractTableModel {
    Q_OBJECT

    SymbolTableView* m_parent;
    std::string m_filter;
    std::vector<SharedCacheAPI::CacheSymbol> m_symbols;

public:
    explicit SymbolTableModel(SymbolTableView* parent);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void updateSymbols();

    void setFilter(std::string text);

    const SharedCacheAPI::CacheSymbol& symbolAt(int row) const;
};


class SymbolTableView : public QTableView, public FilterTarget
{
    Q_OBJECT
    friend class SymbolTableModel;

    // TODO: Both the model and the view store the symbols?
    std::vector<SharedCacheAPI::CacheSymbol> m_symbols;

    SymbolTableModel* m_model;

public:
    SymbolTableView(QWidget* parent);
    virtual ~SymbolTableView() override;

    void scrollToFirstItem() override {
        if (model()->rowCount() > 0) {
            scrollTo(model()->index(0, 0));
        }
    }

    // Call this to populate the symbols from the given view.
    void populateSymbols(BinaryNinja::BinaryView& view);

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

    SharedCacheAPI::CacheSymbol getSymbolAtRow(int row) const
    {
        return m_model->symbolAt(row);
    }

    void setFilter(const std::string& filter) override;
};