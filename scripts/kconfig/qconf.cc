/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <qapplication.h>
#include <qmainwindow.h>
#include <qtoolbar.h>
#include <qvbox.h>
#include <qsplitter.h>
#include <qlistview.h>
#include <qtextview.h>
#include <qlineedit.h>
#include <qmenubar.h>
#include <qmessagebox.h>
#include <qaction.h>
#include <qheader.h>
#include <qfiledialog.h>
#include <qregexp.h>
#include <stdlib.h>

#include "lkc.h"
#include "qconf.h"

#include "qconf.moc"
#include "images.c"

static QApplication *configApp;

/*
 * update all the children of a menu entry
 *   removes/adds the entries from the parent widget as necessary
 *
 * parent: either the menu list widget or a menu entry widget
 * menu: entry to be updated
 */
template <class P>
static void updateMenuList(P* parent, struct menu* menu)
{
	struct menu* child;
	ConfigList* list = parent->listView();
	ConfigItem* item;
	ConfigItem* last;
	bool visible;
	bool showAll = list->showAll;
	enum listMode mode = list->mode;
	enum prop_type type;

	if (!menu) {
		while ((item = parent->firstChild()))
			delete item;
		return;
	}

	last = 0;
	for (child = menu->list; child; child = child->next) {
		item = last ? last->nextSibling() : parent->firstChild();
		type = child->prompt ? child->prompt->type : P_UNKNOWN;

		switch (mode) {
		case menuMode:
			if (type != P_ROOTMENU)
				goto hide;
			break;
		case symbolMode:
			if (type == P_ROOTMENU)
				goto hide;
			break;
		default:
			break;
		}

		visible = menu_is_visible(child);
		if (showAll || visible) {
			if (!item || item->menu != child)
				item = new ConfigItem(parent, last, child);
			item->visible = visible;
			item->updateMenu();

			if (mode == fullMode || mode == menuMode ||
			    (type != P_MENU && type != P_ROOTMENU))
				updateMenuList(item, child);
			else
				updateMenuList(item, 0);
			last = item;
			continue;
		}
	hide:	
		if (item && item->menu == child) {
			last = parent->firstChild();
			if (last == item)
				last = 0;
			else while (last->nextSibling() != item)
				last = last->nextSibling();
			delete item;
		}
	}
}

#if QT_VERSION >= 300
/*
 * set the new data
 * TODO check the value
 */
void ConfigItem::okRename(int col)
{
	Parent::okRename(col);
	sym_set_string_value(menu->sym, text(dataColIdx).latin1());
}
#endif

/*
 * update the displayed of a menu entry
 */
void ConfigItem::updateMenu(void)
{
	ConfigList* list;
	struct symbol* sym;
	QString prompt;
	int type;
	enum prop_type ptype;
	tristate expr;
	bool update;

	list = listView();
	update = doInit;
	if (update)
		doInit = false;
	else
		update = list->updateAll;

	sym = menu->sym;
	if (!sym) {
		if (update) {
			setText(promptColIdx, menu_get_prompt(menu));
			ptype = menu->prompt ? menu->prompt->type : P_UNKNOWN;
			if ((ptype == P_ROOTMENU || ptype == P_MENU) &&
			    (list->mode == singleMode || list->mode == symbolMode))
				setPixmap(promptColIdx, list->menuPix);
			else
				setPixmap(promptColIdx, 0);
		}
		return;
	}

	sym_calc_value(sym);
	if (!(sym->flags & SYMBOL_CHANGED) && !update)
		return;

	sym->flags &= ~SYMBOL_CHANGED;

	setText(nameColIdx, menu->sym->name);

	type = sym_get_type(sym);
	switch (type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		char ch;

		prompt = menu_get_prompt(menu);
		if (!sym_is_changable(sym) && !list->showAll) {
			setText(noColIdx, 0);
			setText(modColIdx, 0);
			setText(yesColIdx, 0);
			break;
		}
		expr = sym_get_tristate_value(sym);
		switch (expr) {
		case yes:
			if (sym_is_choice_value(sym) && type == S_BOOLEAN)
				setPixmap(promptColIdx, list->choiceYesPix);
			else
				setPixmap(promptColIdx, list->symbolYesPix);
			setText(yesColIdx, "Y");
			ch = 'Y';
			break;
		case mod:
			setPixmap(promptColIdx, list->symbolModPix);
			setText(modColIdx, "M");
			ch = 'M';
			break;
		default:
			if (sym_is_choice_value(sym) && type == S_BOOLEAN)
				setPixmap(promptColIdx, list->choiceNoPix);
			else
				setPixmap(promptColIdx, list->symbolNoPix);
			setText(noColIdx, "N");
			ch = 'N';
			break;
		}
		if (expr != no)
			setText(noColIdx, sym_tristate_within_range(sym, no) ? "_" : 0);
		if (expr != mod)
			setText(modColIdx, sym_tristate_within_range(sym, mod) ? "_" : 0);
		if (expr != yes)
			setText(yesColIdx, sym_tristate_within_range(sym, yes) ? "_" : 0);

		setText(dataColIdx, QChar(ch));
		break;
	case S_INT:
	case S_HEX:
	case S_STRING:
		const char* data;

		data = sym_get_string_value(sym);
#if QT_VERSION >= 300
		setRenameEnabled(list->mapIdx(dataColIdx), TRUE);
#endif
		setText(dataColIdx, data);
		if (type == S_STRING)
			prompt.sprintf("%s: %s", menu_get_prompt(menu), data);
		else
			prompt.sprintf("(%s) %s", data, menu_get_prompt(menu));
		break;
	}
	if (!sym_has_value(sym) && visible)
		prompt += " (NEW)";
	setText(promptColIdx, prompt);
}

void ConfigItem::paintCell(QPainter* p, const QColorGroup& cg, int column, int width, int align)
{
	ConfigList* list = listView();

	if (visible) {
		if (isSelected() && !list->hasFocus() && list->mode == menuMode)
			Parent::paintCell(p, list->inactivedColorGroup, column, width, align);
		else
			Parent::paintCell(p, cg, column, width, align);
	} else
		Parent::paintCell(p, list->disabledColorGroup, column, width, align);
}

/*
 * construct a menu entry
 */
void ConfigItem::init(void)
{
	ConfigList* list = listView();
#if QT_VERSION < 300
	visible = TRUE;
#endif
	//menu->data = this;
	if (list->mode != fullMode)
		setOpen(TRUE);
	doInit= true;
}

/*
 * destruct a menu entry
 */
ConfigItem::~ConfigItem(void)
{
	//menu->data = 0;
}

void ConfigLineEdit::show(ConfigItem* i)
{
	item = i;
	if (sym_get_string_value(item->menu->sym))
		setText(sym_get_string_value(item->menu->sym));
	else
		setText(0);
	Parent::show();
	setFocus();
}

void ConfigLineEdit::keyPressEvent(QKeyEvent* e)
{
	switch (e->key()) {
	case Key_Escape:
		break;
	case Key_Return:
	case Key_Enter:
		sym_set_string_value(item->menu->sym, text().latin1());
		emit lineChanged(item);
		break;
	default:
		Parent::keyPressEvent(e);
		return;
	}
	e->accept();
	hide();
}

ConfigList::ConfigList(QWidget* p, ConfigView* cv)
	: Parent(p), cview(cv),
	  updateAll(false),
	  symbolYesPix(xpm_symbol_yes), symbolModPix(xpm_symbol_mod), symbolNoPix(xpm_symbol_no),
	  choiceYesPix(xpm_choice_yes), choiceNoPix(xpm_choice_no), menuPix(xpm_menu), menuInvPix(xpm_menu_inv),
	  showAll(false), showName(false), showRange(false), showData(false),
	  rootEntry(0)
{
	int i;

	setSorting(-1);
	setRootIsDecorated(TRUE);
	disabledColorGroup = palette().active();
	disabledColorGroup.setColor(QColorGroup::Text, palette().disabled().text());
	inactivedColorGroup = palette().active();
	inactivedColorGroup.setColor(QColorGroup::Highlight, palette().disabled().highlight());

	connect(this, SIGNAL(selectionChanged(void)),
		SLOT(updateSelection(void)));

	for (i = 0; i < colNr; i++)
		colMap[i] = colRevMap[i] = -1;
	addColumn(promptColIdx, "Option");

	reinit();
}

void ConfigList::reinit(void)
{
	removeColumn(dataColIdx);
	removeColumn(yesColIdx);
	removeColumn(modColIdx);
	removeColumn(noColIdx);
	removeColumn(nameColIdx);

	if (showName)
		addColumn(nameColIdx, "Name");
	if (showRange) {
		addColumn(noColIdx, "N");
		addColumn(modColIdx, "M");
		addColumn(yesColIdx, "Y");
	}
	if (showData)
		addColumn(dataColIdx, "Value");

	updateListAll();
}

void ConfigList::updateSelection(void)
{
	struct menu *menu;
	enum prop_type type;

	ConfigItem* item = (ConfigItem*)selectedItem();
	if (!item)
		return;

	cview->setHelp(item);

	menu = item->menu;
	type = menu->prompt ? menu->prompt->type : P_UNKNOWN;
	if (mode == menuMode && (type == P_MENU || type == P_ROOTMENU))
		emit menuSelected(menu);
}

void ConfigList::updateList(ConfigItem* item)
{
	(void)item;	// unused so far
	updateMenuList(this, rootEntry);
}

void ConfigList::setAllOpen(bool open)
{
	QListViewItemIterator it(this);

	for (; it.current(); it++)
		it.current()->setOpen(open);
}

void ConfigList::setValue(ConfigItem* item, tristate val)
{
	struct symbol* sym;
	int type;
	tristate oldval;

	sym = item->menu->sym;
	if (!sym)
		return;

	type = sym_get_type(sym);
	switch (type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		oldval = sym_get_tristate_value(sym);

		if (!sym_set_tristate_value(sym, val))
			return;
		if (oldval == no && item->menu->list)
			item->setOpen(TRUE);
		emit symbolChanged(item);
		break;
	}
}

void ConfigList::changeValue(ConfigItem* item)
{
	struct symbol* sym;
	struct menu* menu;
	int type, oldexpr, newexpr;

	menu = item->menu;
	sym = menu->sym;
	if (!sym) {
		if (item->menu->list)
			item->setOpen(!item->isOpen());
		return;
	}

	type = sym_get_type(sym);
	switch (type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		oldexpr = sym_get_tristate_value(sym);
		newexpr = sym_toggle_tristate_value(sym);
		if (item->menu->list) {
			if (oldexpr == newexpr)
				item->setOpen(!item->isOpen());
			else if (oldexpr == no)
				item->setOpen(TRUE);
		}
		if (oldexpr != newexpr)
			emit symbolChanged(item);
		break;
	case S_INT:
	case S_HEX:
	case S_STRING:
#if QT_VERSION >= 300
		if (colMap[dataColIdx] >= 0)
			item->startRename(colMap[dataColIdx]);
		else
#endif
			lineEdit->show(item);
		break;
	}
}

void ConfigList::setRootMenu(struct menu *menu)
{
	enum prop_type type;

	if (rootEntry == menu)
		return;
	type = menu && menu->prompt ? menu->prompt->type : P_UNKNOWN;
	if (type != P_MENU && type != P_ROOTMENU)
		return;
	updateMenuList(this, 0);
	rootEntry = menu;
	updateListAll();
	setSelected(currentItem(), hasFocus());
}

void ConfigList::setParentMenu(void)
{
	ConfigItem* item;
	struct menu *oldroot, *newroot;

	oldroot = rootEntry;
	newroot = menu_get_parent_menu(oldroot);
	if (newroot == oldroot)
		return;
	setRootMenu(newroot);

	QListViewItemIterator it(this);
	for (; (item = (ConfigItem*)it.current()); it++) {
		if (item->menu == oldroot) {
			setCurrentItem(item);
			ensureItemVisible(item);
			break;
		}
	}
}

void ConfigList::keyPressEvent(QKeyEvent* ev)
{
	QListViewItem* i = currentItem();
	ConfigItem* item;
	struct menu *menu;
	enum prop_type type;

	if (ev->key() == Key_Escape && mode != fullMode) {
		emit parentSelected();
		ev->accept();
		return;
	}

	if (!i) {
		Parent::keyPressEvent(ev);
		return;
	}
	item = (ConfigItem*)i;

	switch (ev->key()) {
	case Key_Return:
	case Key_Enter:
		menu = item->menu;
		type = menu->prompt ? menu->prompt->type : P_UNKNOWN;
		if ((type == P_MENU || type == P_ROOTMENU) && mode != fullMode) {
			emit menuSelected(menu);
			break;
		}
	case Key_Space:
		changeValue(item);
		break;
	case Key_N:
		setValue(item, no);
		break;
	case Key_M:
		setValue(item, mod);
		break;
	case Key_Y:
		setValue(item, yes);
		break;
	default:
		Parent::keyPressEvent(ev);
		return;
	}
	ev->accept();
}

void ConfigList::contentsMousePressEvent(QMouseEvent* e)
{
	//QPoint p(contentsToViewport(e->pos()));
	//printf("contentsMousePressEvent: %d,%d\n", p.x(), p.y());
	QListView::contentsMousePressEvent(e);
}

void ConfigList::contentsMouseReleaseEvent(QMouseEvent* e)
{
	QPoint p(contentsToViewport(e->pos()));
	ConfigItem* item = (ConfigItem*)itemAt(p);
	struct menu *menu;
	const QPixmap* pm;
	int idx, x;

	if (!item)
		goto skip;

	menu = item->menu;
	x = header()->offset() + p.x();
	idx = colRevMap[header()->sectionAt(x)];
	switch (idx) {
	case promptColIdx:
		pm = item->pixmap(promptColIdx);
		if (pm) {
			int off = header()->sectionPos(0) + itemMargin() +
				treeStepSize() * (item->depth() + (rootIsDecorated() ? 1 : 0));
			if (x >= off && x < off + pm->width()) {
				if (menu->sym)
					changeValue(item);
				else
					emit menuSelected(menu);
			}
		}
		break;
	case noColIdx:
		setValue(item, no);
		break;
	case modColIdx:
		setValue(item, mod);
		break;
	case yesColIdx:
		setValue(item, yes);
		break;
	case dataColIdx:
		changeValue(item);
		break;
	}

skip:
	//printf("contentsMouseReleaseEvent: %d,%d\n", p.x(), p.y());
	QListView::contentsMouseReleaseEvent(e);
}

void ConfigList::contentsMouseMoveEvent(QMouseEvent* e)
{
	//QPoint p(contentsToViewport(e->pos()));
	//printf("contentsMouseMoveEvent: %d,%d\n", p.x(), p.y());
	QListView::contentsMouseMoveEvent(e);
}

void ConfigList::contentsMouseDoubleClickEvent(QMouseEvent* e)
{
	QPoint p(contentsToViewport(e->pos()));
	ConfigItem* item = (ConfigItem*)itemAt(p);
	struct menu *menu;
	enum prop_type ptype;

	if (!item)
		goto skip;
	menu = item->menu;
	ptype = menu->prompt ? menu->prompt->type : P_UNKNOWN;
	if ((ptype == P_ROOTMENU || ptype == P_MENU) &&
	    (mode == singleMode || mode == symbolMode))
		emit menuSelected(menu);

skip:
	//printf("contentsMouseDoubleClickEvent: %d,%d\n", p.x(), p.y());
	QListView::contentsMouseDoubleClickEvent(e);
}

void ConfigList::focusInEvent(QFocusEvent *e)
{
	Parent::focusInEvent(e);

	QListViewItem* item = currentItem();
	if (!item)
		return;

	setSelected(item, TRUE);
	emit gotFocus();
}

/*
 * Construct the complete config widget
 */
ConfigView::ConfigView(void)
{
	QMenuBar* menu;
	QSplitter* split1;
	QSplitter* split2;

	showDebug = false;

	split1 = new QSplitter(this);
	split1->setOrientation(QSplitter::Horizontal);
	setCentralWidget(split1);

	menuList = new ConfigList(split1, this);

	split2 = new QSplitter(split1);
	split2->setOrientation(QSplitter::Vertical);

	// create config tree
	QVBox* box = new QVBox(split2);
	configList = new ConfigList(box, this);
	configList->lineEdit = new ConfigLineEdit(box);
	configList->lineEdit->hide();
	configList->connect(configList, SIGNAL(symbolChanged(ConfigItem*)),
			    configList, SLOT(updateList(ConfigItem*)));
	configList->connect(configList, SIGNAL(symbolChanged(ConfigItem*)),
			    menuList, SLOT(updateList(ConfigItem*)));
	configList->connect(configList->lineEdit, SIGNAL(lineChanged(ConfigItem*)),
			    SLOT(updateList(ConfigItem*)));

	helpText = new QTextView(split2);
	helpText->setTextFormat(Qt::RichText);

	setTabOrder(configList, helpText);
	configList->setFocus();

	menu = menuBar();
	toolBar = new QToolBar("Tools", this);

	backAction = new QAction("Back", QPixmap(xpm_back), "Back", 0, this);
	  connect(backAction, SIGNAL(activated()), SLOT(goBack()));
	  backAction->setEnabled(FALSE);
	QAction *quitAction = new QAction("Quit", "&Quit", CTRL+Key_Q, this);
	  connect(quitAction, SIGNAL(activated()), SLOT(close()));
	QAction *loadAction = new QAction("Load", QPixmap(xpm_load), "&Load", CTRL+Key_L, this);
	  connect(loadAction, SIGNAL(activated()), SLOT(loadConfig()));
	QAction *saveAction = new QAction("Save", QPixmap(xpm_save), "&Save", CTRL+Key_S, this);
	  connect(saveAction, SIGNAL(activated()), SLOT(saveConfig()));
	QAction *saveAsAction = new QAction("Save As...", "Save &As...", 0, this);
	  connect(saveAsAction, SIGNAL(activated()), SLOT(saveConfigAs()));
	QAction *singleViewAction = new QAction("Single View", QPixmap(xpm_single_view), "Split View", 0, this);
	  connect(singleViewAction, SIGNAL(activated()), SLOT(showSingleView()));
	QAction *splitViewAction = new QAction("Split View", QPixmap(xpm_split_view), "Split View", 0, this);
	  connect(splitViewAction, SIGNAL(activated()), SLOT(showSplitView()));
	QAction *fullViewAction = new QAction("Full View", QPixmap(xpm_tree_view), "Full View", 0, this);
	  connect(fullViewAction, SIGNAL(activated()), SLOT(showFullView()));

	QAction *showNameAction = new QAction(NULL, "Show Name", 0, this);
	  showNameAction->setToggleAction(TRUE);
	  showNameAction->setOn(configList->showName);
	  connect(showNameAction, SIGNAL(toggled(bool)), SLOT(setShowName(bool)));
	QAction *showRangeAction = new QAction(NULL, "Show Range", 0, this);
	  showRangeAction->setToggleAction(TRUE);
	  showRangeAction->setOn(configList->showRange);
	  connect(showRangeAction, SIGNAL(toggled(bool)), SLOT(setShowRange(bool)));
	QAction *showDataAction = new QAction(NULL, "Show Data", 0, this);
	  showDataAction->setToggleAction(TRUE);
	  showDataAction->setOn(configList->showData);
	  connect(showDataAction, SIGNAL(toggled(bool)), SLOT(setShowData(bool)));
	QAction *showAllAction = new QAction(NULL, "Show All Options", 0, this);
	  showAllAction->setToggleAction(TRUE);
	  showAllAction->setOn(configList->showAll);
	  connect(showAllAction, SIGNAL(toggled(bool)), SLOT(setShowAll(bool)));
	QAction *showDebugAction = new QAction(NULL, "Show Debug Info", 0, this);
	  showDebugAction->setToggleAction(TRUE);
	  showDebugAction->setOn(showDebug);
	  connect(showDebugAction, SIGNAL(toggled(bool)), SLOT(setShowDebug(bool)));

	// init tool bar
	backAction->addTo(toolBar);
	toolBar->addSeparator();
	loadAction->addTo(toolBar);
	saveAction->addTo(toolBar);
	toolBar->addSeparator();
	singleViewAction->addTo(toolBar);
	splitViewAction->addTo(toolBar);
	fullViewAction->addTo(toolBar);

	// create config menu
	QPopupMenu* config = new QPopupMenu(this);
	menu->insertItem("&File", config);
	loadAction->addTo(config);
	saveAction->addTo(config);
	saveAsAction->addTo(config);
	config->insertSeparator();
	quitAction->addTo(config);

	// create options menu
	QPopupMenu* optionMenu = new QPopupMenu(this);
	menu->insertItem("&Option", optionMenu);
	showNameAction->addTo(optionMenu);
	showRangeAction->addTo(optionMenu);
	showDataAction->addTo(optionMenu);
	optionMenu->insertSeparator();
	showAllAction->addTo(optionMenu);
	showDebugAction->addTo(optionMenu);

	connect(configList, SIGNAL(menuSelected(struct menu *)),
		SLOT(changeMenu(struct menu *)));
	connect(configList, SIGNAL(parentSelected()),
		SLOT(goBack()));
	connect(menuList, SIGNAL(menuSelected(struct menu *)),
		SLOT(changeMenu(struct menu *)));

	connect(configList, SIGNAL(gotFocus(void)),
		SLOT(listFocusChanged(void)));
	connect(menuList, SIGNAL(gotFocus(void)),
		SLOT(listFocusChanged(void)));

	//showFullView();
	showSplitView();
}

static QString print_filter(const char *str)
{
	QRegExp re("[<>&\"\\n]");
	QString res = str;
	for (int i = 0; (i = res.find(re, i)) >= 0;) {
		switch (res[i].latin1()) {
		case '<':
			res.replace(i, 1, "&lt;");
			i += 4;
			break;
		case '>':
			res.replace(i, 1, "&gt;");
			i += 4;
			break;
		case '&':
			res.replace(i, 1, "&amp;");
			i += 5;
			break;
		case '"':
			res.replace(i, 1, "&quot;");
			i += 6;
			break;
		case '\n':
			res.replace(i, 1, "<br>");
			i += 4;
			break;
		}
	}
	return res;
}

static void expr_print_help(void *data, const char *str)
{
	((QString*)data)->append(print_filter(str));
}

/*
 * display a new help entry as soon as a new menu entry is selected
 */
void ConfigView::setHelp(QListViewItem* item)
{
	struct symbol* sym;
	struct menu* menu;

	configList->lineEdit->hide();
	if (item) {
		QString head, debug, help;
		menu = ((ConfigItem*)item)->menu;
		sym = menu->sym;
		if (sym) {
			if (menu->prompt) {
				head += "<big><b>";
				head += print_filter(menu->prompt->text);
				head += "</b></big>";
				if (sym->name) {
					head += " (";
					head += print_filter(sym->name);
					head += ")";
				}
			} else if (sym->name) {
				head += "<big><b>";
				head += print_filter(sym->name);
				head += "</b></big>";
			}
			head += "<br><br>";

			if (showDebug) {
				debug += "type: ";
				debug += print_filter(sym_type_name(sym->type));
				debug += "<br>";
				for (struct property *prop = sym->prop; prop; prop = prop->next) {
					switch (prop->type) {
					case P_PROMPT:
						debug += "prompt: ";
						debug += print_filter(prop->text);
						debug += "<br>";
						if (prop->visible.expr) {
							debug += "&nbsp;&nbsp;dep: ";
							expr_print(prop->visible.expr, expr_print_help, &debug, E_NONE);
							debug += "<br>";
						}
						break;
					case P_DEFAULT:
						debug += "default: ";
						if (sym_is_choice(sym))
							debug += print_filter(prop->def->name);
						else {
							sym_calc_value(prop->def);
							debug += print_filter(sym_get_string_value(prop->def));
						}
						debug += "<br>";
						if (prop->visible.expr) {
							debug += "&nbsp;&nbsp;dep: ";
							expr_print(prop->visible.expr, expr_print_help, &debug, E_NONE);
							debug += "<br>";
						}
						break;
					case P_CHOICE:
						break;
					default:
						debug += "unknown property: ";
						debug += prop_get_type_name(prop->type);
						debug += "<br>";
					}
				}
				debug += "<br>";
			}

			help = print_filter(sym->help);
		} else if (menu->prompt) {
			head += "<big><b>";
			head += print_filter(menu->prompt->text);
			head += "</b></big><br><br>";
			if (showDebug) {
				if (menu->prompt->visible.expr) {
					debug += "&nbsp;&nbsp;dep: ";
					expr_print(menu->prompt->visible.expr, expr_print_help, &debug, E_NONE);
					debug += "<br>";
				}
			}
		}
		helpText->setText(head + debug + help);
		return;
	}
	helpText->setText(NULL);
}

void ConfigView::loadConfig(void)
{
	QString s = QFileDialog::getOpenFileName(".config", NULL, this);
	if (s.isNull())
		return;
	if (conf_read(s.latin1()))
		QMessageBox::information(this, "qconf", "Unable to load configuration!");
}

void ConfigView::saveConfig(void)
{
	if (conf_write(NULL))
		QMessageBox::information(this, "qconf", "Unable to save configuration!");
}

void ConfigView::saveConfigAs(void)
{
	QString s = QFileDialog::getSaveFileName(".config", NULL, this);
	if (s.isNull())
		return;
	if (conf_write(s.latin1()))
		QMessageBox::information(this, "qconf", "Unable to save configuration!");
}

void ConfigView::changeMenu(struct menu *menu)
{
	configList->setRootMenu(menu);
	backAction->setEnabled(TRUE);
}

void ConfigView::listFocusChanged(void)
{
	if (menuList->hasFocus()) {
		if (menuList->mode == menuMode)
			configList->clearSelection();
		setHelp(menuList->selectedItem());
	} else if (configList->hasFocus()) {
		setHelp(configList->selectedItem());
	}
}

void ConfigView::goBack(void)
{
	ConfigItem* item;

	configList->setParentMenu();
	if (configList->rootEntry == &rootmenu)
		backAction->setEnabled(FALSE);
	item = (ConfigItem*)menuList->selectedItem();
	while (item) {
		if (item->menu == configList->rootEntry) {
			menuList->setSelected(item, TRUE);
			break;
		}
		item = (ConfigItem*)item->parent();
	}
}

void ConfigView::showSingleView(void)
{
	menuList->hide();
	menuList->setRootMenu(0);
	configList->mode = singleMode;
	if (configList->rootEntry == &rootmenu)
		configList->updateListAll();
	else
		configList->setRootMenu(&rootmenu);
	configList->setAllOpen(TRUE);
	configList->setFocus();
}

void ConfigView::showSplitView(void)
{
	configList->mode = symbolMode;
	if (configList->rootEntry == &rootmenu)
		configList->updateListAll();
	else
		configList->setRootMenu(&rootmenu);
	configList->setAllOpen(TRUE);
	configApp->processEvents();
	menuList->mode = menuMode;
	menuList->setRootMenu(&rootmenu);
	menuList->show();
	menuList->setAllOpen(TRUE);
	menuList->setFocus();
}

void ConfigView::showFullView(void)
{
	menuList->hide();
	menuList->setRootMenu(0);
	configList->mode = fullMode;
	if (configList->rootEntry == &rootmenu)
		configList->updateListAll();
	else
		configList->setRootMenu(&rootmenu);
	configList->setAllOpen(FALSE);
	configList->setFocus();
}

void ConfigView::setShowAll(bool b)
{
	if (configList->showAll == b)
		return;
	configList->showAll = b;
	configList->updateListAll();
	menuList->showAll = b;
	menuList->updateListAll();
}

void ConfigView::setShowDebug(bool b)
{
	if (showDebug == b)
		return;
	showDebug = b;
}

void ConfigView::setShowName(bool b)
{
	if (configList->showName == b)
		return;
	configList->showName = b;
	configList->reinit();
}

void ConfigView::setShowRange(bool b)
{
	if (configList->showRange == b)
		return;
	configList->showRange = b;
	configList->reinit();
}

void ConfigView::setShowData(bool b)
{
	if (configList->showData == b)
		return;
	configList->showData = b;
	configList->reinit();
}

/*
 * ask for saving configuration before quitting
 * TODO ask only when something changed
 */
void ConfigView::closeEvent(QCloseEvent* e)
{
	if (!sym_change_count) {
		e->accept();
		return;
	}
	QMessageBox mb("qconf", "Save configuration?", QMessageBox::Warning,
			QMessageBox::Yes | QMessageBox::Default, QMessageBox::No, QMessageBox::Cancel | QMessageBox::Escape);
	mb.setButtonText(QMessageBox::Yes, "&Save Changes");
	mb.setButtonText(QMessageBox::No, "&Discard Changes");
	mb.setButtonText(QMessageBox::Cancel, "Cancel Exit");
	switch (mb.exec()) {
	case QMessageBox::Yes:
		conf_write(NULL);
	case QMessageBox::No:
		e->accept();
		break;
	case QMessageBox::Cancel:
		e->ignore();
		break;
	}
}

void fixup_rootmenu(struct menu *menu)
{
	struct menu *child;

	if (!menu->prompt || menu->prompt->type != P_MENU)
		return;
	menu->prompt->type = P_ROOTMENU;
	for (child = menu->list; child; child = child->next)
		fixup_rootmenu(child);
}

int main(int ac, char** av)
{
	ConfigView* v;
	const char *name;

#ifndef LKC_DIRECT_LINK
	kconfig_load();
#endif

	configApp = new QApplication(ac, av);
	if (ac > 1 && av[1][0] == '-') {
		switch (av[1][1]) {
		case 'a':
			//showAll = 1;
			break;
		case 'h':
		case '?':
			printf("%s <config>\n", av[0]);
			exit(0);
		}
		name = av[2];
	} else
		name = av[1];
	conf_parse(name);
	fixup_rootmenu(&rootmenu);
	conf_read(NULL);
	//zconfdump(stdout);
	v = new ConfigView();

	//zconfdump(stdout);
	v->show();
	configApp->connect(configApp, SIGNAL(lastWindowClosed()), SLOT(quit()));
	configApp->exec();
	return 0;
}
