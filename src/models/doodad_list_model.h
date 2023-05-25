#pragma once

#include <QIdentityProxyModel>
#include <QSortFilterProxyModel>

#include <QSize>
#include <QMargins>
#include <QIcon>

#include <unordered_map>


#include "unordered_dense.h"

import QIconResource;

class DoodadListModel : public QIdentityProxyModel {
	Q_OBJECT

public:
	explicit DoodadListModel(QObject* parent = nullptr);

	QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override;
	QModelIndex mapToSource(const QModelIndex& proxyIndex) const override;

	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	Qt::ItemFlags flags(const QModelIndex& index) const override;

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;

	QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
	QModelIndex parent(const QModelIndex& child) const override;

private:
	std::unordered_map<char, std::shared_ptr<QIconResource>> icons;
};

class DoodadListFilter : public QSortFilterProxyModel {
	bool filterAcceptsRow(int sourceRow,const QModelIndex& sourceParent) const override;
	bool lessThan(const QModelIndex& left,const QModelIndex& right) const override;
	QString filterCategory = "";
	char filterTileset = '*';

public:
	void setFilterCategory(QString category);
	void setFilterTileset(char tileset);
	using QSortFilterProxyModel::QSortFilterProxyModel;
};