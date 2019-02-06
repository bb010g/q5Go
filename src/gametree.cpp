/*
* gametree.cpp
*/

#include "config.h"
#include "setting.h"
#include "goboard.h"
#include "gogame.h"
#include "gametree.h"
#include "mainwindow.h"
#include "svgbuilder.h"

static QByteArray box_svg =
	"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>"
	"<svg width=\"160\" height=\"160\">"
	"  <path"
	"     style=\"fill:#a89e97;fill-rule:evenodd;stroke:none\""
	"     d=\"M 80,50 160,25 80,0 0,25 Z\" />"
	"  <path"
	"     style=\"fill:#917c6f;fill-rule:evenodd;stroke:none\""
	"     d=\"M 80,150 160,125 160,25 80,50 Z\" />"
	"  <path"
	"     style=\"fill:#483e37;fill-rule:evenodd;stroke:none\""
	"     d=\"M 80,50 80,150 0,125 0,25 Z\" />"
	"  <path"
	"     style=\"fill:#FFFF00;fill-rule:evenodd;stroke:none\""
	"     d=\"M 35.2,14 44.8,11 124.8,36 115.2,35 Z\" />"
	"</svg>";

class ClickablePixmap : public QGraphicsPixmapItem
{
	GameTree *m_view;
	int m_x, m_y, m_size;
public:
	ClickablePixmap (GameTree *view, QGraphicsScene *scene, int x, int y, int size, const QPixmap &pm)
		: QGraphicsPixmapItem (pm), m_view (view), m_x (x), m_y (y), m_size (size)
	{
		setZValue (10);
		/* Supposedly faster, and makes it easier to click on edit nodes.  */
		setShapeMode (QGraphicsPixmapItem::BoundingRectShape);
		scene->addItem (this);
		setAcceptHoverEvents (true);
	}
protected:
	virtual void mousePressEvent (QGraphicsSceneMouseEvent *e) override;
	virtual void hoverEnterEvent (QGraphicsSceneHoverEvent *e) override;
	virtual void hoverLeaveEvent (QGraphicsSceneHoverEvent *e) override;
	virtual void contextMenuEvent (QGraphicsSceneContextMenuEvent *e) override;
};

void ClickablePixmap::contextMenuEvent (QGraphicsSceneContextMenuEvent *e)
{
	QPointF pos = e->pos ();
	int x = m_x + pos.x () / m_size;
	int y = m_y;
	m_view->show_menu (x, y, e->screenPos ());
}

void ClickablePixmap::mousePressEvent (QGraphicsSceneMouseEvent *e)
{
	QPointF pos = e->pos ();
	int x = m_x + pos.x () / m_size;
	int y = m_y;
	if (e->button () == Qt::LeftButton) {
		if (e->modifiers () == Qt::ShiftModifier)
			m_view->toggle_collapse (x, y, false);
		else if (e->modifiers () == Qt::ControlModifier)
			m_view->toggle_collapse (x, y, true);
		else
			m_view->item_clicked (x, y);
	} else if (e->button () == Qt::MiddleButton)
		m_view->toggle_collapse (x, y, false);
}

void ClickablePixmap::hoverEnterEvent (QGraphicsSceneHoverEvent *)
{
	m_view->setDragMode (QGraphicsView::NoDrag);
}

void ClickablePixmap::hoverLeaveEvent (QGraphicsSceneHoverEvent *)
{
	m_view->setDragMode (QGraphicsView::ScrollHandDrag);
}

GameTree::GameTree (MainWindow *win, QWidget *parent)
	: QGraphicsView (parent), m_win (win), m_header_view (Qt::Horizontal, this)
{
	setFocusPolicy (Qt::NoFocus);
	m_scene = new QGraphicsScene (0, 0, 30, 30, this);
	setScene (m_scene);
	setBackgroundBrush (QBrush (Qt::white));

	setAlignment (Qt::AlignTop | Qt::AlignLeft);

	setDragMode (QGraphicsView::ScrollHandDrag);
	setToolTip (tr ("The game tree.\nClick nodes to move to them, click empty areas to drag.\n"
			"Shift-click or middle-click nodes to collapse or expand their sub-variations.\n"
			"Control-click a collapsed node to expand one level of its children."));

	m_header_view.setModel (&m_headers);
	m_header_view.setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
	int frame_width = frameWidth ();
	m_header_view.resize (width () - 2 * frame_width, m_header_view.height ());
	m_header_view.move (frame_width, frame_width);

	setViewportMargins (0, m_header_view.height (), 0, 0);
	QScrollBar *hscr = horizontalScrollBar ();
	connect (hscr, &QScrollBar::valueChanged, [=] (int v) { m_header_view.setOffset (v); });
	update_prefs ();
}

void GameTree::update_prefs ()
{
	m_hide_diags = setting->readBoolEntry ("GAMETREE_DIAGHIDE");
	m_size = setting->readIntEntry ("GAMETREE_SIZE");
	m_size = std::max (30, std::min (120, m_size));

	QFont f = setting->fontStandard;
	QFontMetrics fm (f);

	int width = 0;
	for (int i = 0; i < 9; i++) {
		char c = '0' + i;
		char str[] = { c, c, c, '\0' };
		QRect br = fm.boundingRect (str);
		width = std::max (width, br.width ());
	}
	if (width > m_size) {
		double ratio = m_size * 1.0 / width;
		/* Don't scale too heavily.  */
		ratio = std::max (0.5, ratio);
		f.setPointSize (f.pointSize () * ratio);
		m_header_view.setFont (f);
	} else
		m_header_view.setFont (f);

	int ssize = m_size - 2;
	int soff = ssize / 2;
	svg_builder wstone (ssize, ssize);
	wstone.circle_at (soff, soff, soff * 0.9 - 1, "white", "black", "2");
	svg_builder bstone (ssize, ssize);
	bstone.circle_at (soff, soff, soff * 0.9, "black", "none");
	svg_builder wfig (ssize, ssize);
	wfig.square_at (soff, soff, ssize * 0.9 - 1, "white", "black");
	svg_builder bfig (ssize, ssize);
	bfig.square_at (soff, soff, ssize * 0.9, "black", "none");
	svg_builder edit (ssize, ssize);
	edit.circle_at (soff / 2, soff / 2, (ssize / 4) * 0.9, "black", "none");
	edit.circle_at (ssize - soff / 2, ssize - soff / 2, (ssize / 4) * 0.9, "black", "none");
	edit.circle_at (soff / 2, ssize - soff / 2, (ssize / 4) * 0.9 - 0.5, "white", "black", "1");
	edit.circle_at (ssize - soff / 2, soff / 2, (ssize / 4) * 0.9 - 0.5, "white", "black", "1");
	m_pm_w = QPixmap (wstone.to_pixmap (ssize, ssize));
	m_pm_b = QPixmap (bstone.to_pixmap (ssize, ssize));
	m_pm_wfig = QPixmap (wfig.to_pixmap (ssize, ssize));
	m_pm_bfig = QPixmap (bfig.to_pixmap (ssize, ssize));
	m_pm_e = QPixmap (edit.to_pixmap (ssize, ssize));

	QSvgRenderer renderer (box_svg);
	m_pm_box = QPixmap (ssize, ssize);
	m_pm_box.fill (QColor (0, 0, 0, 0));
	QPainter painter;
	painter.begin (&m_pm_box);
	renderer.render (&painter);
	painter.end ();

	if (m_game != nullptr)
		update (m_game, m_active, true);
}

void GameTree::resizeEvent(QResizeEvent*)
{
	int frame_width = frameWidth ();
	m_header_view.resize (width () - 2 * frame_width, m_header_view.height ());
}

QSize GameTree::sizeHint () const
{
	return QSize (100, 100);
}

void GameTree::toggle_collapse (int x, int y, bool one_level)
{
	game_state *st = m_game->get_root ()->locate_by_vis_coords (x, y, 0, 0);
	if (one_level) {
		if (!st->vis_expand_one ())
			return;
	} else
		st->toggle_vis_collapse ();
	update (m_game, m_active);
}

void GameTree::toggle_figure (int x, int y)
{
	game_state *st = m_game->get_root ()->locate_by_vis_coords (x, y, 0, 0);
	if (st->has_figure ())
		st->clear_figure ();
	else
		st->set_figure (256, "");
	update (m_game, m_active);
}

void GameTree::item_clicked (int x, int y)
{
	if (m_game == nullptr)
		return;

	game_state *st = m_game->get_root ()->locate_by_vis_coords (x, y, 0, 0);
	if (st == m_active)
		return;
	/* Have to call this first so we trace the correct path - transfer_observers
	   eventually ends up calling into our update function.  */
	st->make_active ();
	m_active->transfer_observers (st);
}

void GameTree::show_menu (int x, int y, const QPoint &pos)
{
	game_state *st = m_game->get_root ()->locate_by_vis_coords (x, y, 0, 0);
	QMenu menu;
	if (st->vis_collapsed ()) {
		menu.addAction (QIcon (m_pm_box), QObject::tr ("Expand subtree"), [=] () { toggle_collapse (x, y, false); });
		menu.addAction (QObject::tr ("Expand one level of child nodes"), [=] () { toggle_collapse (x, y, true); });
	} else
		menu.addAction (QIcon (m_pm_box), QObject::tr ("Collapse subtree"), [=] () { toggle_collapse (x, y, false); });
	if (st->has_figure ())
		menu.addAction (QIcon (":/BoardWindow/images/boardwindow/figure.png"),
				QObject::tr("Clear diagram status for this node"),
				[=] () { toggle_figure (x, y); m_win->update_figures (m_active); });
	else
		menu.addAction (QIcon (":/BoardWindow/images/boardwindow/figure.png"),
				QObject::tr("Set this move to be the start of a diagram"),
				[=] () { toggle_figure (x, y); m_win->update_figures (m_active); });
	menu.addAction (QObject::tr ("Navigate to this node"), [=] () { item_clicked (x, y); });
	menu.exec (pos);
}

void GameTree::update (std::shared_ptr<game_record> gr, game_state *active, bool force)
{
	game_state *r = gr->get_root ();
	bool changed = r->update_visualization (m_hide_diags) || force;
	bool active_changed = m_active != active;
	if (gr != m_game)
		changed = true;
	if (!changed && !active_changed)
		return;
	m_game = gr;
	m_active = active;
	if (changed) {
		const visual_tree &vroot = r->visualization ();
		int w = vroot.width ();
		int h = vroot.height ();

		m_scene->setSceneRect (0, 0, m_size * w, m_size * h);
		setSceneRect (0, 0, m_size * w, m_size * h);

		m_scene->clear ();
		m_sel = nullptr;
		m_path = nullptr;
		m_path_end = nullptr;

		setDragMode (QGraphicsView::ScrollHandDrag);

		visual_tree::bit_rect stones_w (w, h);
		visual_tree::bit_rect stones_b (w, h);
		visual_tree::bit_rect edits (w, h);
		visual_tree::bit_rect collapsed (w, h);
		visual_tree::bit_rect figures (w, h);
		visual_tree::bit_rect hidden_figs (w, h);

		r->extract_visualization (0, 0, stones_w, stones_b, edits, collapsed, figures, hidden_figs);
		visual_tree::bit_rect all_items = stones_w;
		all_items.ior (stones_b, 0, 0);
		all_items.ior (edits, 0, 0);
		all_items.ior (collapsed, 0, 0);

		QPen diag_pen (Qt::blue);
		diag_pen.setWidth (2);
		for (int y = 0; y < h; y++)
			for (int x0 = 0; x0 < w; ) {
				int len = 1;
				if (all_items.test_bit (x0, y)) {
					while (x0 + len < w && all_items.test_bit (x0 + len, y))
						len++;
					QPixmap combined (m_size * len, m_size);
					combined.fill (QColor (0, 0, 0, 0));
					QPainter painter;
					painter.begin (&combined);
					painter.setPen (Qt::NoPen);
					for (int i = 0; i < len; i++) {
						QPixmap *src = &m_pm_box;
						bool fig = figures.test_bit (x0 + i, y);
						if (edits.test_bit (x0 + i, y))
							src =  &m_pm_e;
						else if (stones_w.test_bit (x0 + i, y))
							src = fig ? &m_pm_wfig : &m_pm_w;
						else if (stones_b.test_bit (x0 + i, y))
							src = fig ? &m_pm_bfig : &m_pm_b;
						painter.drawPixmap (i * m_size + 1, 1, *src);
						if (hidden_figs.test_bit (x0 + i, y)) {
							painter.setPen (diag_pen);
							painter.drawRect (i * m_size + m_size / 2 + 2, 2,
									  m_size / 2 - 4, m_size / 2 - 4);
							painter.setPen (Qt::NoPen);
						}
					}
					painter.end ();
					QGraphicsPixmapItem *pm = new ClickablePixmap (this, m_scene, x0, y, m_size, combined);
					pm->setPos (x0 * m_size, y * m_size);
				}
				x0 += len;
			}
		auto line = [&] (int x0, int y0, int x1, int y1, bool dotted) -> void
			{
				QPen pen;
				pen.setWidth (2);
				if (dotted)
					pen.setStyle (Qt::DotLine);
				QLineF line (x0, y0, x1, y1);
				m_scene->addLine (line, pen);
			};
		r->render_visualization (m_size / 2, m_size / 2, m_size, line, true);

		m_header_view.setDefaultSectionSize (m_size);
		m_header_view.setSectionResizeMode (QHeaderView::Fixed);

		/* ??? Uncertain whether this is helpful for performance.
		   The idea is to avoid many dataChanged calls.  */
		m_header_view.setModel (nullptr);
		for (int i = 0; i < w; i++)
			m_headers.setHorizontalHeaderItem (i, new QStandardItem (QString::number (i)));
		m_headers.setColumnCount (w);
		m_header_view.setModel (&m_headers);
	}

	QPen pen;
	pen.setColor (Qt::red);
	pen.setWidth (4);
	QPainterPath path;
	bool first = false;
	auto point = [&] (int x, int y) -> void
		{
			if (first)
				path.moveTo (x, y);
			else
				path.lineTo (x, y);
		};
	auto dotted_line = [&] (int x0, int y0, int x1, int y1, bool) -> void
		{
			QLineF line (x0, y0, x1, y1);
			QPen new_pen = pen;
			new_pen.setStyle (Qt::DotLine);
			delete m_path_end;
			m_path_end = m_scene->addLine (line, new_pen);
		};

	delete m_path_end;
	m_path_end = nullptr;
	r->render_active_trace (m_size / 2, m_size / 2, m_size, point, dotted_line);
	delete m_path;
	m_path = m_scene->addPath (path, pen);
	m_path->setZValue (3);

	int acx = 0, acy = 0;
	bool found = r->locate_visual (0, 0, active, acx, acy);

	/* Ideally we'd keep the m_sel object around and just move it.  The problem is that when scrolling,
	   Qt leaves behind ghost copies at the old location as graphical garbage.  */
	delete m_sel;
	m_sel = m_scene->addRect (0, 0, m_size, m_size, Qt::NoPen, QBrush (Qt::red));
	m_sel->setZValue (-1);
	if (found) {
		m_sel->setPos (acx * m_size, acy * m_size);
		if (active_changed)
			ensureVisible (m_sel);
	} else
		m_sel->hide ();
	m_scene->update ();
}
