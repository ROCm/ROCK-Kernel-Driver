/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <qlistview.h>

class ConfigLineEdit;
class ConfigItem;
class ConfigView;

enum colIdx {
	promptColIdx, nameColIdx, noColIdx, modColIdx, yesColIdx, dataColIdx, colNr
};
enum listMode {
	singleMode, menuMode, symbolMode, fullMode
};

class ConfigList : public QListView {
	Q_OBJECT
	typedef class QListView Parent;
public:
	ConfigList(QWidget* p, ConfigView* cview);
	void reinit(void);

	ConfigLineEdit* lineEdit;
protected:
	ConfigView* cview;

	void keyPressEvent(QKeyEvent *e);
	void contentsMousePressEvent(QMouseEvent *e);
	void contentsMouseReleaseEvent(QMouseEvent *e);
	void contentsMouseMoveEvent(QMouseEvent *e);
	void contentsMouseDoubleClickEvent(QMouseEvent *e);
	void focusInEvent(QFocusEvent *e);
public slots:
	void setRootMenu(struct menu *menu);

	void updateList(ConfigItem *item);
	void setValue(ConfigItem* item, tristate val);
	void changeValue(ConfigItem* item);
	void updateSelection(void);
signals:
	void menuSelected(struct menu *menu);
	void parentSelected(void);
	void symbolChanged(ConfigItem* item);
	void gotFocus(void);

public:
	void updateListAll(void)
	{
		updateAll = true;
		updateList(NULL);
		updateAll = false;
	}
	ConfigList* listView()
	{
		return this;
	}
	ConfigItem* firstChild() const
	{
		return (ConfigItem *)Parent::firstChild();
	}
	int mapIdx(colIdx idx)
	{
		return colMap[idx];
	}
	void addColumn(colIdx idx, const QString& label)
	{
		colMap[idx] = Parent::addColumn(label);
		colRevMap[colMap[idx]] = idx;
	}
	void removeColumn(colIdx idx)
	{
		int col = colMap[idx];
		if (col >= 0) {
			Parent::removeColumn(col);
			colRevMap[col] = colMap[idx] = -1;
		}
	}
	void setAllOpen(bool open);
	void setParentMenu(void);

	bool updateAll;

	QPixmap symbolYesPix, symbolModPix, symbolNoPix;
	QPixmap choiceYesPix, choiceNoPix, menuPix, menuInvPix;

	bool showAll, showName, showRange, showData;
	enum listMode mode;
	struct menu *rootEntry;
	QColorGroup disabledColorGroup;
	QColorGroup inactivedColorGroup;

private:
	int colMap[colNr];
	int colRevMap[colNr];
};

class ConfigItem : public QListViewItem {
	typedef class QListViewItem Parent;
public:
	ConfigItem(QListView *parent, ConfigItem *after, struct menu *m)
	: Parent(parent, after), menu(m)
	{
		init();
	}
	ConfigItem(ConfigItem *parent, ConfigItem *after, struct menu *m)
	: Parent(parent, after), menu(m)
	{
		init();
	}
	~ConfigItem(void);
	void init(void);
#if QT_VERSION >= 300
	void okRename(int col);
#endif
	void updateMenu(void);
	ConfigList* listView() const
	{
		return (ConfigList*)Parent::listView();
	}
	ConfigItem* firstChild() const
	{
		return (ConfigItem *)Parent::firstChild();
	}
	ConfigItem* nextSibling() const
	{
		return (ConfigItem *)Parent::nextSibling();
	}
	void setText(colIdx idx, const QString& text)
	{
		Parent::setText(listView()->mapIdx(idx), text);
	}
	QString text(colIdx idx) const
	{
		return Parent::text(listView()->mapIdx(idx));
	}
	void setPixmap(colIdx idx, const QPixmap& pm)
	{
		Parent::setPixmap(listView()->mapIdx(idx), pm);
	}
	const QPixmap* pixmap(colIdx idx) const
	{
		return Parent::pixmap(listView()->mapIdx(idx));
	}
	void paintCell(QPainter* p, const QColorGroup& cg, int column, int width, int align);

	struct menu *menu;
	bool visible;
	bool doInit;
};

class ConfigLineEdit : public QLineEdit {
	Q_OBJECT
	typedef class QLineEdit Parent;
public:
	ConfigLineEdit(QWidget * parent)
	: QLineEdit(parent)
	{ }
	void show(ConfigItem *i);
	void keyPressEvent(QKeyEvent *e);
signals:
	void lineChanged(ConfigItem *item);

public:
	ConfigItem *item;
};

class ConfigView : public QMainWindow {
	Q_OBJECT
public:
	ConfigView(void);
public slots:
	void setHelp(QListViewItem* item);
	void changeMenu(struct menu *);
	void listFocusChanged(void);
	void goBack(void);
	void loadConfig(void);
	void saveConfig(void);
	void saveConfigAs(void);
	void showSingleView(void);
	void showSplitView(void);
	void showFullView(void);
	void setShowAll(bool);
	void setShowDebug(bool);
	void setShowRange(bool);
	void setShowName(bool);
	void setShowData(bool);

protected:
	void closeEvent(QCloseEvent *e);

	ConfigList *menuList;
	ConfigList *configList;
	QTextView *helpText;
	QToolBar *toolBar;
	QAction *backAction;

	bool showDebug;
};
