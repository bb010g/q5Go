/*
* board.cpp
*/
#include <vector>
#include <fstream>
#include <cmath>

#include <qmessagebox.h>
#include <qapplication.h>
#include <qclipboard.h>
#include <qpainter.h>
#include <qlineedit.h>
#include <qcursor.h>

#include <QPixmap>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QWheelEvent>

#include "config.h"
#include "setting.h"
#include "qgo.h"
#include "board.h"
#include "grid.h"
#include "imagehandler.h"
#include "tip.h"
#include "mainwindow.h"
#include "clientwin.h"
#include "miscdialogs.h"
#include "svgbuilder.h"
#include "ui_helpers.h"

Board::Board(QWidget *parent, QGraphicsScene *c)
	: QGraphicsView(c, parent), Gtp_Controller (parent)
{
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	viewport()->setMouseTracking(true);
	setUpdatesEnabled(true);

	board_size = DEFAULT_BOARD_SIZE;
	showCoords = setting->readBoolEntry("BOARD_COORDS");
	showSGFCoords = setting->readBoolEntry("SGF_BOARD_COORDS");
	antiClicko = setting->readBoolEntry("ANTICLICKO");

	setStyleSheet( "QGraphicsView { border-style: none; }" );

	// Create an ImageHandler instance.
	imageHandler = new ImageHandler();
	CHECK_PTR(imageHandler);

	// Init the canvas
	canvas = new QGraphicsScene (0, 0, BOARD_X, BOARD_Y, this);
	setScene (canvas);

	// Init the grid size and the imagehandler pixmaps
	calculateSize();

	imageHandler->init(square_size);

	// Initialize some class variables
	isModified = false;
	mouseState = Qt::NoButton;

	//coordsTip = new Tip(this);
#ifdef Q_OS_WIN
	resizeDelayFlag = false;
#endif
	curX = curY = -1;

	lockResize = false;
	navIntersectionStatus = false;

	canvas->addItem (m_mark_layer = new QGraphicsPixmapItem ());
	m_mark_layer->setZValue (20);

	canvas->addItem (coverTop = new QGraphicsRectItem ());
	canvas->addItem (coverBot = new QGraphicsRectItem ());
	canvas->addItem (coverLeft = new QGraphicsRectItem ());
	canvas->addItem (coverRight = new QGraphicsRectItem ());

	coverTop->setBrush (QColor (0, 0, 0, 128));
	coverBot->setBrush (QColor (0, 0, 0, 128));
	coverLeft->setBrush (QColor (0, 0, 0, 128));
	coverRight->setBrush (QColor (0, 0, 0, 128));
	coverTop->setPen (QColor (0, 0, 0, 0));
	coverBot->setPen (QColor (0, 0, 0, 0));
	coverLeft->setPen (QColor (0, 0, 0, 0));
	coverRight->setPen (QColor (0, 0, 0, 0));

	setFocusPolicy(Qt::NoFocus);
}

Board::~Board()
{
	delete m_grid;
	delete m_coords;
	clear_eval_data ();
	clear_stones ();
	delete canvas;
	delete imageHandler;
	delete m_analyzer;
}

// distance from table edge to wooden board edge
const int Board::margin = 2;

// distance from coords to surrounding elements
const int Board::coord_margin = 4;

void Board::calculateSize()
{
	// Calculate the size values

	int w = canvas->width ();
	int h = canvas->height ();

	int table_size = (w < h ? w : h) - margin * 2;

	QGraphicsSimpleTextItem coordV (QString::number(board_size));
	QGraphicsSimpleTextItem coordH ("A");
	canvas->addItem (&coordV);
	canvas->addItem (&coordH);
	int coord_width = (int)coordV.boundingRect().width();
	int coord_height = (int)coordH.boundingRect().height();

	// space for coodinates if shown
	coord_offset = coord_width < coord_height ? coord_height : coord_width;

	square_size = table_size;
	// we need 1 more virtual 'square' for the stones on 1st and last line getting off the grid
	if (showCoords)
		square_size -= 2 * (coord_offset + coord_margin * 2);

	square_size /= (float)board_size;
	// Should not happen, but safe is safe.
	if (square_size == 0)
		  square_size = 1;

	// grid size
	int board_pixel_size = square_size * (board_size - 1);

	// Center the board in canvas

	m_wood_rect = QRect ((w - table_size) / 2, (h - table_size) / 2, table_size, table_size);
	m_board_rect = QRect ((w - board_pixel_size) / 2, (h - board_pixel_size) / 2,
			      board_pixel_size, board_pixel_size);
}

void Board::resizeBoard (int w, int h)
{
	if (w < 30 || h < 30)
		return;

	// Resize canvas
	canvas->setSceneRect (0, 0, w, h);

	// Recalculate the size values
	calculateSize ();

	// Rescale the pixmaps in the ImageHandler
	imageHandler->rescale (square_size);

	// Redraw the board
	draw_background ();
	draw_grid_and_coords ();
	updateCovers ();

	sync_appearance ();
}

void Board::resizeEvent(QResizeEvent*)
{
#ifdef _WS_WIN_x
	if (!resizeDelayFlag)
	{
		resizeDelayFlag = true;
		// not necessary?
		QTimer::singleShot(50, this, SLOT(changeSize()));
	}
#else
	if (!lockResize)
		changeSize();
#endif
}

void Board::draw_background()
{
	int w = canvas->width ();
	int h = canvas->height ();

	m_wood = *setting->wood_image ();
	m_table = *setting->table_image ();

	// Create pixmap of appropriate size
	//QPixmap all(w, h);
	QImage image (w, h, QImage::Format_RGB32);

	// Paint table and board on the pixmap
	QPainter painter;

	painter.begin (&image);
	painter.setPen (Qt::NoPen);

	int board_x0 = m_wood_rect.x ();
	int board_y0 = m_wood_rect.y ();
	int board_x1 = board_x0 + m_wood_rect.width () - 1;
	int board_y1 = board_y0 + m_wood_rect.height () - 1;
	painter.drawTiledPixmap (0, 0, w, h, m_table);
	painter.drawTiledPixmap (m_wood_rect, m_wood);

	// Modify the edges of the board so they appear slightly three-dimensional.
	int width = 3;

	for (int i = 0; i < width; i++) {
		double darken_factor = (double) (width - i) / width;
		double lighten_factor = (double) (width - i) / width;
		darken_factor *= darken_factor;
		lighten_factor *= lighten_factor;
		darken_factor = darken_factor * 0.6;
		lighten_factor = lighten_factor * 0.4;
		QColor darken (0, 0, 0, darken_factor * 255);
		QColor lighten (255, 255, 255, lighten_factor * 255);
		painter.setPen (lighten);
		painter.drawLine (board_x0 + i, board_y0 + i, board_x1 - i, board_y0 + i);
		painter.drawLine (board_x1 - i, board_y0 + i + 1, board_x1 - i, board_y1 - i);
		painter.setPen (darken);
		painter.drawLine (board_x0 + i, board_y0 + i + 1, board_x0 + i, board_y1 - i);
		painter.drawLine (board_x0 + i + 1, board_y1 - i, board_x1 - i - 1, board_y1 - i);
	}

	// Draw a shadow below the board
	width = 10;
	for (int i = 0; i < width; i++) {
		double darken_factor = (double) (width - i) / width;
		darken_factor = darken_factor * 0.5;
		QColor darken (0, 0, 0, darken_factor * 255);
		painter.setPen (darken);
		painter.drawLine (board_x0 - i, board_y0 + i, board_x0 - i, board_y1 + i);
		painter.drawLine (board_x0 - i + 1, board_y1 + i, board_x1 - i, board_y1 + i);
	}
	painter.end ();

	canvas->setBackgroundBrush (QBrush (image));
}

void Board::draw_grid_and_coords ()
{
	m_grid->resize (m_board_rect, square_size);
	m_coords->resize (m_wood_rect, m_board_rect, square_size, showCoords);
}

/* Handle a click on the Done button.  Return true if we should return to normal mode.  */
bool Board::doCountDone()
{
	game_state *new_st = m_state->add_child_edit (*m_edit_board, m_edit_to_move, true);
	m_state->transfer_observers (new_st);

	return true;
}

void Board::setMode (GameMode mode)
{
	game_state *new_st = m_state;

	GameMode old_mode = m_game_mode;
	m_game_mode = mode;

	if (mode == modeScore || mode == modeScoreRemote || mode == modeEdit) {
		m_edit_board = new go_board (m_state->get_board ());
		m_edit_changed = false;
		m_edit_to_move = m_state->to_move ();
		if (mode == modeScore || mode == modeScoreRemote) {
			m_edit_board->calc_scoring_markers_complex ();
		}
	} else if (old_mode == modeScore || old_mode == modeScoreRemote) {
		/* The only way the scored board is added to the game tree is through
		   doCountDone.  This may have happened at this point; in any case,
		   discard the board now.  */
		delete m_edit_board;
		m_edit_board = nullptr;
 	} else if (mode == modeNormal && old_mode == modeEdit) {
		m_edit_board->identify_units ();
		/* Normally, we add an edited board as a new child.  However, when the original position
		   was the root node, or an edit node, and has no children yet, just update it in-place.
		   It can be debated whether this behaviour would be surprising to users, but it seems like
		   the most useful way to handle the situation.  */
		if (m_state->n_children () == 0
		    && (m_state->root_node_p () || m_state->was_edit_p ()))
		{
			m_state->replace (*m_edit_board, m_edit_to_move);
		} else if (m_state->to_move () != m_edit_to_move || m_state->get_board () != *m_edit_board)
			new_st = m_state->add_child_edit (*m_edit_board, m_edit_to_move);
		delete m_edit_board;
		m_edit_board = nullptr;
	}
	if (new_st != m_state)
		m_state->transfer_observers (new_st);

	/* Always needed when changing modes to update toolbar buttons etc.  */
	sync_appearance (false);
}

bool Board::show_cursor_p ()
{
	if (!setting->readBoolEntry("CURSOR") || navIntersectionStatus)
		return false;

	if (m_mark_rect || m_request_mark_rect)
		return false;

	GameMode mode = m_game_mode;
	if (m_edit_mark != mark::none)
		return false;

	if (mode == modeScore || mode == modeObserve)
		return false;

	if (!player_to_move_p ())
		return false;

	return true;
}

static QString convert_letter_mark (mextra extra)
{
	if (extra < 26)
		return QString ('A' + extra);
	else
		return QString ('a' + extra - 26);
}

/* Render a mark in SVG at center position CX/CY in a square with a side length of FACTOR.
   M and ME represent the mark as present in the board.  SC is the color of the stone that
   has been rendered before, or NONE.  COUNT_VAL, if nonzero, together with MAX_NUMBER represents
   the move number to be displayed.  VAR_M and VAR_ME represent a variation mark, typically
   some letter.

   WHITE_BACKGROUND is true if we are doing this for svg export, it changes the display a
   little bit.  */
static bool add_mark_svg (svg_builder &svg, double cx, double cy, double factor,
			  mark m, mextra me, const std::string &mstr, mark var_m, mextra var_me,
			  stone_color sc, int count_val, int max_number,
			  bool was_last_move, bool white_background, const QFontInfo &fi)
{
	QString mark_col = sc == black ? "white" : "black";
	if (sc == none && white_background && (count_val != 0 || m != mark::none || var_m != mark::none)) {
		svg.square_at (cx, cy, factor * 0.9,
			       "white", "none");
	}
	if (count_val != 0) {
		int len = max_number > 99 ? 3 : max_number > 9 ? 2 : 1;
		svg.text_at (cx, cy, factor,
			     len, QString::number (count_val),
			     mark_col, fi);
		return true;
	}

	if (m == mark::none && var_m != mark::none) {
		m = var_m;
		me = var_me;
		mark_col = "blue";
	}

	/* We make an artificial mark for the last move.  Done late so as to not
	   override other marks.  */
	if (m == mark::none && was_last_move)
		m = mark::move;

	/* Convert the large number of conceptual marks into a smaller set of
	   visual ones.  */

	/* @@@ Could think about making these two look different.  Using red
	   as a color, for example, but that makes it less visible.  */
	if (m == mark::move) {
		m = mark::circle;
	} else if (m == mark::falseeye) {
		m = mark::triangle;
		mark_col = "red";
	} else if (m == mark::seki) {
		m = mark::square;
		mark_col = "blue";
	}

	if (m == mark::none)
		return false;

	switch (m) {
	case mark::circle:
		svg.circle_at (cx, cy, factor * 0.25,
			       "none", mark_col);
		break;
	case mark::square:
		svg.square_at (cx, cy, factor * 0.8 / M_SQRT2,
			       "none", mark_col);
		break;
	case mark::triangle:
		svg.triangle_at (cx, cy, factor * 0.8,
				 "none", mark_col);
		break;
	case mark::terr:
		if (sc == none && white_background)
			mark_col = "black";
		else
			mark_col = me == 0 ? "white" : "black";
		/* fall through */
	case mark::cross:
		svg.cross_at (cx, cy, factor * 0.8,
			      mark_col);
		break;
	case mark::num:
		svg.text_at (cx, cy, factor, 0,
			     QString::number (me),
			     mark_col, fi);
		break;
	case mark::letter:
		svg.text_at (cx, cy, factor, 0,
			     convert_letter_mark (me),
			     mark_col, fi);
		break;
	case mark::text:
		svg.text_at (cx, cy, factor, 0,
			     QString::fromStdString (mstr),
			     mark_col, fi);
		break;

	default:
		break;
	}
	return true;
}

QByteArray Board::render_svg (bool do_number, bool coords)
{
	const go_board &b = m_edit_board == nullptr ? m_state->get_board () : *m_edit_board;
	/* Look back through previous moves to see if we should do numbering.  */
	int n_back = 0;
	int *count_map = new int[board_size * board_size]();
	game_state *startpos = nullptr;
	bool numbering = do_number && m_edit_board == nullptr;

	if (numbering && !m_state->get_start_count () && m_state->was_move_p ()) {
		startpos = m_state;
		while (startpos
		       && (startpos->was_move_p () || startpos->root_node_p ())
		       && !startpos->get_start_count ())
		{
			if (startpos->root_node_p ()) {
				startpos = nullptr;
				break;
			}
			int x = startpos->get_move_x ();
			int y = startpos->get_move_y ();
			int bp = b.bitpos (x, y);
			count_map[bp] = ++n_back;
			startpos = startpos->prev_move ();
		}
		if (startpos && !startpos->was_move_p () && !startpos->root_node_p ())
			startpos = nullptr;
	}

	double factor = 30;
	double margin = 10;
	double offset_x = margin + factor / 2;
	double offset_y = margin + factor / 2;
	int cols = (m_rect_x2 - m_rect_x1 + 1);
	int rows = (m_rect_y2 - m_rect_y1 + 1);
	int w = factor * cols + 2 * margin;
	int h = factor * rows + 2 * margin;
	if (coords) {
		if (m_rect_x1 == 1)
			offset_x += factor, w += factor;
		if (m_rect_y1 == 1)
			offset_y += factor, h += factor;
		if (m_rect_x2 == board_size)
			w += factor;
		if (m_rect_y2 == board_size)
			h += factor;
	}

	QFontInfo fi (setting->fontMarks);
	svg_builder svg (w, h);

	/* A white background, since use white squares to clear the grid when showing marks.
	   Against a clear background that wouldn't look very good.  */
	svg.rect (0, 0, w, h, "white", "none");

	if (coords) {
		double dist = margin + factor / 2;
		for (int y = 0; y < rows; y++) {
			int ry = y + m_rect_y1;
			double center_y = offset_y + y * factor;
			if (m_rect_x1 == 1)
				svg.text_at (dist, center_y, factor,
					     board_size < 10 ? 1 : 2, QString::number (board_size - ry + 1),
					     "black", fi);
			if (m_rect_x2 == board_size)
				svg.text_at (w - dist , center_y, factor,
					     board_size < 10 ? 1 : 2, QString::number (board_size - ry + 1),
					     "black", fi);
		}
		for (int x = 0; x < cols; x++) {
			int rx = x + m_rect_x1;
			double center_x = offset_x + x * factor;
			QString label;
			if (rx < 9)
				label = QChar ('A' + rx - 1);
			else
				label = QChar ('A' + rx);
			if (m_rect_y1 == 1)
				svg.text_at (center_x, dist, factor,
					     board_size < 10 ? 1 : 2, label, "black", fi);
			if (m_rect_y2 == board_size)
				svg.text_at (center_x, h - dist, factor,
					     board_size < 10 ? 1 : 2, label, "black", fi);
		}
	}

	/* The grid.  */
	int top = m_rect_y1 > 1 ? -factor / 2 : 0;
	int bot = m_rect_y2 < board_size ? factor / 2 : 0;
	int lef = m_rect_x1 > 1 ? -factor / 2 : 0;
	int rig = m_rect_x2 < board_size ? factor / 2 : 0;
	for (int x = 0; x < cols; x++) {
		QString width = "1";
		if ((x == 0 && m_rect_x1 == 1) || (x + 1 == cols && m_rect_x2 == board_size))
			width = "2";
		svg.line (offset_x + x * factor, offset_y + top,
			  offset_x + x * factor, offset_y + (rows - 1) * factor + bot,
			  "black", width);
	}
	for (int y = 0; y < rows; y++) {
		QString width = "1";
		if ((y == 0 && m_rect_y1 == 1) || (y + 1 == rows && m_rect_y2 == board_size))
			width = "2";
		svg.line (offset_x + lef, offset_y + y * factor,
			  offset_x + (cols - 1) * factor + rig, offset_y + y * factor,
			  "black", width);
	}
	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			int rx = x + m_rect_x1 - 1;
			int ry = y + m_rect_y1 - 1;
			stone_color c = b.stone_at (rx, ry);
			mark m = b.mark_at (rx, ry);
			mextra extra = b.mark_extra_at (rx, ry);
			int bp = b.bitpos (rx, ry);
			int v = startpos == nullptr ? 0 : count_map[bp];

			double center_x = offset_x + x * factor;
			double center_y = offset_y + y * factor;
			if (c != none) {
				svg.circle_at (center_x, center_y, factor * 0.45,
					       c == black ? "black" : "white",
					       c == black ? "none" : "black");
			}
			if (v > 0)
				v = n_back - v + 1;
			add_mark_svg (svg, center_x, center_y, factor,
				      m, extra, m == mark::text ? b.mark_text_at (rx, ry) : "",
				      mark::none, 0, c, v, n_back, false, true, fi);
		}
	}

	delete[] count_map;
	return svg;
}

/* Construct ASCII diagrams suitable for use on lifein19x19.com.
   Moves can be numbered 1-10.  When we do numbering, we split the moves up
   into a suitable number of diagrams, inserting breaks when 10 moves are
   exceeded or a stone is placed on an intersection which previously
   held something else.

   This should really be part of game_state, but there is the added complication
   of the m_edit_board.  */

QString Board::render_ascii (bool do_number, bool coords)
{
	int sz = m_game->boardsize ();
	QString result;

	int *count_map = new int[sz * sz]();
	game_state *startpos = m_state;
	if (do_number && m_edit_board == nullptr && !m_state->get_start_count ()) {
		startpos = m_state;
		while (startpos
		       && (startpos->was_move_p () || startpos->root_node_p ())
		       && !startpos->get_start_count ())
		{
			startpos = startpos->prev_move ();
		}
		if (startpos == nullptr || (!startpos->was_move_p () && !startpos->root_node_p ()))
			startpos = m_state;
	}
	int moves = 1;
	do {
		const go_board &b = m_edit_board == nullptr ? startpos->get_board () : *m_edit_board;

		int n_mv = 0;
		game_state *next = startpos;
		memset (count_map, 0, sz * sz * sizeof *count_map);
		while (next != m_state && n_mv < 10) {
			game_state *nx2 = next->next_move ();
			int x = nx2->get_move_x ();
			int y = nx2->get_move_y ();
			int bp = b.bitpos (x, y);
			if (count_map[bp] != 0 || b.stone_at (x, y) != none)
				break;
			next = nx2;
			count_map[bp] = ++n_mv;
		}

		result += "[go]$$";
		result += startpos->to_move () == black ? "B" : "W";
		if (coords) {
			result += "c" + QString::number (sz);
		}
		if (moves > 1) {
			result += "m" + QString::number (moves);
		}
		result += "\n";
		if (m_rect_y1 == 1) {
			result += "$$";
			if (m_rect_x1 == 1)
				result += " +";
			for (int i = 0; i < m_rect_x2 - m_rect_x1 + 1; i++)
				result += "--";
			if (m_rect_x2 == sz)
				result += "-+";
			result += "\n";
		}
		for (int y = m_rect_y1; y <= m_rect_y2; y++) {
			result += "$$";
			if (m_rect_x1 == 1)
				result += " |";
			for (int x = m_rect_x1; x <= m_rect_x2; x++) {
				int bp = b.bitpos (x - 1, y - 1);
				int v = count_map[bp];
				if (v != 0) {
					result += " " + QString::number (v % 10);
				} else {
					stone_color c = b.stone_at (x - 1, y - 1);
					mark m = b.mark_at (x - 1, y - 1);
					mextra me = b.mark_extra_at (x - 1, y - 1);
					char rslt[] = " .";
					if (c == none) {
						if (m == mark::letter && me < 26)
							rslt[1] = 'a' + me;
						else switch (m) {
						case mark::circle: rslt[1] = 'C'; break;
						case mark::square: rslt[1] = 'S'; break;
						case mark::triangle: rslt[1] = 'T'; break;
						case mark::cross: rslt[1] = 'M'; break;
						default: break;
						}
					} else {
						switch (m) {
						case mark::square: rslt[1] = c == black ? '#' : '@'; break;
						case mark::triangle: rslt[1] = c == black ? 'Y' : 'Q'; break;
						case mark::circle: rslt[1] = c == black ? 'B' : 'W'; break;
						case mark::cross: rslt[1] = c == black ? 'Z' : 'P'; break;
						default: rslt[1] = c == black ? 'X' : 'O'; break;
						}
					}
					result += rslt;
				}
			}
			if (m_rect_x2 == sz)
				result += " |";
			result += "\n";
		}
		if (m_rect_y2 == sz) {
			result += "$$";
			if (m_rect_x1 == 1)
				result += " +";
			for (int i = 0; i < m_rect_x2 - m_rect_x1 + 1; i++)
				result += "--";
			if (m_rect_x2 == sz)
				result += "-+";
			result += "\n";
		}
		result += "[/go]\n";
		startpos = next;
		moves += n_mv;
	} while (startpos != m_state);
	return result;
}

/* The central function for synchronizing visual appearance with the abstract board data.  */
void Board::sync_appearance (bool board_only)
{
	bool have_analysis = m_eval_state != nullptr;
	bool numbering = !have_analysis && m_edit_board == nullptr;

	bool analysis_hide = setting->readBoolEntry ("ANALYSIS_HIDEOTHER");
	bool analysis_children = setting->readBoolEntry ("ANALYSIS_CHILDREN");
	int analysis_vartype = setting->readIntEntry ("ANALYSIS_VARTYPE");
	int winrate_for = setting->readIntEntry ("ANALYSIS_WINRATE");
	int maxdepth = setting->readIntEntry ("ANALYSIS_DEPTH");
	stone_color wr_swap_col = winrate_for == 0 ? white : winrate_for == 1 ? black : none;

	const go_board &b = m_edit_board == nullptr ? m_state->get_board () : *m_edit_board;
	stone_color to_move = m_edit_board == nullptr ? m_state->to_move () : m_edit_to_move;

	int var_type = have_analysis && analysis_children ? 0 : m_vars_type;

	const go_board child_vars = m_state->child_moves (nullptr);
	const go_board sibling_vars = m_state->sibling_moves ();
	const go_board &vars = m_vars_children ? child_vars : sibling_vars;
	const bit_array st_w = b.get_stones_w ();
	const bit_array st_b = b.get_stones_b ();
	int sz = b.size ();

	/* Builds a mark layer, which gets rendered into a pixmap and added to the canvas.
	   The factor is the size of a square in svg, it gets scaled later.  It should have
	   an optically pleasant relation with the stroke width (2 for marks).  */
	double svg_factor = 30;
	QFontInfo fi (setting->fontMarks);
	svg_builder svg (svg_factor * sz, svg_factor * sz);

	/* Look back through previous moves to see if we should do numbering.  */
	int n_back = 0, max_number = 0;
	int *count_map = new int[sz * sz]();

	game_state *startpos = nullptr;
	if (have_analysis) {
		startpos = m_eval_state;
		game_state *pv = m_eval_state->find_child_move (curX, curY);
		if (pv == nullptr) {
			m_main_widget->set_2nd_eval (nullptr, 0, none, 0);
		} else {
			int x = pv->get_move_x ();
			int y = pv->get_move_y ();
			int bp = b.bitpos (x, y);
			if (x > 7)
				x++;
			QString move = QChar ('A' + x) + QString::number (board_size - y);
			m_main_widget->set_2nd_eval (move, m_primary_eval + m_winrate[bp],
						     m_state->to_move (), m_visits[bp]);
		}
		int depth = 0;
		while (pv && (maxdepth == 0 || depth++ < maxdepth)) {
			int x = pv->get_move_x ();
			int y = pv->get_move_y ();
			int bp = b.bitpos (x, y);
			count_map[bp] = ++n_back;
			pv = pv->next_move ();
		}
		max_number = n_back;
		n_back = 0;
	}

	if (numbering && !m_state->get_start_count () && m_state->was_move_p ()) {
		startpos = m_state;
		while (startpos
		       && (startpos->was_move_p () || startpos->root_node_p ())
		       && !startpos->get_start_count ())
		{
			if (startpos->root_node_p ()) {
				startpos = nullptr;
				break;
			}
			int x = startpos->get_move_x ();
			int y = startpos->get_move_y ();
			int bp = b.bitpos (x, y);
			count_map[bp] = ++n_back;
			startpos = startpos->prev_move ();
		}
		if (startpos && !startpos->was_move_p () && !startpos->root_node_p ())
			startpos = nullptr;
		max_number = n_back;
	}
	m_used_letters.clear ();
	m_used_numbers.clear ();

	m_grid->showAll ();

	for (int x = 0; x < sz; x++)
		for (int y = 0; y < sz; y++) {
			int bp = b.bitpos (x, y);
			stone_gfx *s = m_stones[bp];
			stone_color sc = b.stone_at (x, y);
			stone_type type = stone_type::live;
			mark mark_at_pos = b.mark_at (x, y);
			mextra extra = b.mark_extra_at (x, y);
			bool was_last_move = false;
			int v = startpos ? count_map[bp] : 0;

			if (m_edit_board == nullptr && m_state->was_move_p ()) {
				int last_x = m_state->get_move_x ();
				int last_y = m_state->get_move_y ();
				if (last_x == x && last_y == y)
					was_last_move = true;
			}

			/* If we don't have a real stone, check for various possibilities of
			   ghost stones.  First, PV moves, then the mouse cursor, then territory
			   marks, then variation display.  */
			if (sc == none && n_back == 0 && v > 0) {
				int v_tmp = v;
				if (m_eval_state->to_move () == black)
					v_tmp++;
				sc = v_tmp % 2 ? white : black;
			}

			if (sc == none) {
				if (x == curX && y == curY && show_cursor_p ()) {
					sc = to_move;
					/* @@@ */
					if (m_game_mode == modeEdit)
						sc = black;
					type = stone_type::var;
				}
			} else if (mark_at_pos == mark::terr || mark_at_pos == mark::falseeye)
				type = stone_type::var;

			if (sc == none && var_type == 1) {
				sc = vars.stone_at (x, y);
				if (sc != none)
					type = stone_type::var;
			}
			if (sc == none) {
				if (s) {
					s->hide ();
					s = nullptr;
				}
			}
			if (sc != none) {
				if (s == nullptr)
					s = new stone_gfx (canvas, imageHandler, sc, type, bp);
				else
					s->set_appearance (sc, type);
				s->show ();
			}
			if (s) {
				s->set_center(m_board_rect.x () + square_size * x, m_board_rect.y () + square_size * y);
				m_stones[bp] = s;
			}

			mark var_mark = var_type == 2 ? vars.mark_at (x, y) : mark::none;
			mextra var_me = vars.mark_extra_at (x, y);
			/* Now look at marks.  */

			if (mark_at_pos == mark::num)
				m_used_numbers.set_bit (extra);
			else if (mark_at_pos == mark::letter)
				m_used_letters.set_bit (extra);

			mark eval_mark = m_eval_state != nullptr ? m_eval_state->get_board ().mark_at (x, y) : mark::none;
			mextra eval_me = m_eval_state != nullptr ? m_eval_state->get_board ().mark_extra_at (x, y) : 0;

			double cx = svg_factor / 2 + svg_factor * x;
			double cy = svg_factor / 2 + svg_factor * y;

			if (v > 0 && n_back != 0)
				v = n_back - v + 1;
			bool added = false;
			bool an_child_mark = have_analysis && analysis_children && v == 0 && child_vars.stone_at (x, y) == to_move;
			/* Put a percentage or letter mark on variations returned by the analysis engine,
			   unless we are already showing a numbered variation and this intersection
			   has a number.  */
			if (eval_mark != mark::none && v == 0 && (max_number == 0 || !analysis_hide)) {
				QString wr_col = "lightblue";
				double wrdiff = m_winrate[bp];
				if (eval_me > 0) {
					/* m_winrate holds the difference to the primary move's winrate.
					   Use green for a difference of 0, red for any loss bigger than 12%.
					   The HSV angle between red and green is 120, so just multiply by 1000.  */
					int angle = std::min (120.0, std::max (0.0, 120 + 1000 * wrdiff));
					QColor col = QColor::fromHsv (angle, 255, 200);
					wr_col = col.name ();
				}
				svg.circle_at (cx, cy, svg_factor * 0.45, wr_col, an_child_mark ? "white" : "black", "1");

				if (analysis_vartype == 0) {
					QChar c = eval_me >= 26 ? 'a' + eval_me - 26 : 'A' + eval_me;
					svg.text_at (cx, cy, svg_factor, 0, c,
						     "black", fi);
				} else {
					if (analysis_vartype == 2) {
						wrdiff += m_primary_eval;

						if (m_eval_state->to_move () == wr_swap_col)
							wrdiff = 1 - wrdiff;
					} else if (m_eval_state->to_move () == wr_swap_col)
						wrdiff = -wrdiff;

					svg.text_at (cx, cy, svg_factor, 4,
						     QString::number (wrdiff * 100, 'f', 1),
						     "black", fi);
				}
				added = true;
			} else if (an_child_mark) {
				svg.circle_at (cx, cy, svg_factor * 0.45, "none", "white", "1");
			} else
				added = add_mark_svg (svg, cx, cy, svg_factor,
						      mark_at_pos, extra,
						      mark_at_pos == mark::text ? b.mark_text_at (x, y) : "",
						      var_mark, var_me,
						      sc, v, max_number, was_last_move, false, fi);

			if (added)
				m_grid->hide (x, y);
		}

	updateCanvas();

	m_main_widget->recalc_scores (b);
	if (!board_only)
		m_board_win->setMoveData (*m_state, b, m_game_mode);
	delete[] count_map;

	QPixmap img = svg.to_pixmap (square_size * board_size, square_size * board_size);
	m_mark_layer->setPixmap (img);
	m_mark_layer->setPos (m_board_rect.x () - square_size / 2, m_board_rect.y () - square_size / 2);
}

void Board::observed_changed ()
{
	setup_analyzer_position ();
	sync_appearance (false);
	m_board_win->update_game_tree (m_state);
}

#ifndef NO_DEBUG
void Board::debug()
{
    qDebug("Board::debug()");
}
#endif

void Board::deleteNode()
{
	game_state *st = m_state;
	if (st->root_node_p ())
		return;
	game_state *parent = st->prev_move ();
	delete st;
	if (m_state != parent)
		throw std::logic_error ("should have updated to parent");
	const go_board &b = m_state->get_board ();
	m_board_win->setMoveData (*m_state, b, m_game_mode);
	setModified ();
}

void Board::leaveEvent(QEvent*)
{
	curX = curY = -1;
	sync_appearance (true);
}

int Board::convertCoordsToPoint(int c, int o)
{
	int p = c - o + square_size/2;
	if (p >= 0)
		return p / square_size + 1;
	else
		return -1;
}

void Board::updateCovers ()
{
	QRectF sceneRect = canvas->sceneRect ();
	int top_edge = 0;
	if (m_rect_y1 > 1)
		top_edge = m_board_rect.y () + square_size * (m_rect_y1 - 1.5);
	int bot_edge = sceneRect.bottom();
	if (m_rect_y2 < board_size)
		bot_edge = m_board_rect.y () + square_size * (m_rect_y2 - 0.5);
	int left_edge = 0;
	if (m_rect_x1 > 1)
		left_edge = m_board_rect.x () + square_size * (m_rect_x1 - 1.5);
	int right_edge = sceneRect.right();
	if (m_rect_x2 < board_size)
		right_edge = m_board_rect.x () + square_size * (m_rect_x2 - 0.5);

	coverLeft->setVisible (m_rect_x1 > 1);
	coverRight->setVisible (m_rect_x2 < board_size);
	coverTop->setVisible (m_rect_y1 > 1);
	coverBot->setVisible (m_rect_y2 < board_size);

	coverTop->setRect (0, 0, sceneRect.right(), top_edge);
	coverBot->setRect (0, bot_edge, sceneRect.right(), sceneRect.bottom () - bot_edge);
	coverLeft->setRect (0, top_edge, left_edge, bot_edge - top_edge);
	coverRight->setRect (right_edge, top_edge, sceneRect.right() - right_edge, bot_edge - top_edge);
}

void Board::updateRectSel (int x, int y)
{
	if (x < 1)
		x = 1;
	if (y < 1)
		y = 1;
	if (x > board_size)
		x = board_size;
	if (y > board_size)
		y = board_size;
	int minx = m_down_x;
	int miny = m_down_y;
	if (x < minx)
		std::swap (minx, x);
	if (y < miny)
		std::swap (miny, y);
	m_rect_x1 = minx;
	m_rect_y1 = miny;
	m_rect_x2 = x;
	m_rect_y2 = y;
	updateCovers ();
}

void Board::mouseMoveEvent(QMouseEvent *e)
{
	int x = convertCoordsToPoint (e->x (), m_board_rect.x ());
	int y = convertCoordsToPoint (e->y (), m_board_rect.y ());

	if (m_mark_rect)
		updateRectSel (x, y);

	// Outside the valid board?
	if (x < 1 || x > board_size || y < 1 || y > board_size)
	{
		x = -1;
		y = -1;
	} else {
		x--;
		y--;
	}


	if (curX == x && curY == y)
		return;
#if 0 /* Would be nice to make this work, but things are fast enough as-is.  */
	int prev_x = curX;
	int prev_y = curY;
#endif
	curX = x;
	curY = y;

	// Update the statusbar coords tip
	emit coordsChanged (x, y, board_size, showSGFCoords);
#if 0
	sync_appearance (prev_x, prev_y);
	sync_appearance (x, y);
#else
	sync_appearance (true);
#endif
}

void Board::wheelEvent(QWheelEvent *e)
{
	// leave if observing or playing
	if (m_game_mode != modeObserve && m_game_mode != modeNormal)
		return;

#if 0 /* Let's try without this, and with cumulative angles instead.  */
	// Check delay
	if (QTime::currentTime() < wheelTime)
		return;
#endif
	// Needs an extra check on variable mouseState as state() does not work on Windows.
	QPoint numDegrees = e->angleDelta();
	m_cumulative_delta += numDegrees.y ();
	if (m_cumulative_delta < -60) // wheel down to next
	{
		if (e->buttons () == Qt::RightButton || mouseState == Qt::RightButton)
			next_variation ();
		else
			next_move ();
		m_cumulative_delta = 0;
	} else if (m_cumulative_delta > 60) {
		if (e->buttons () == Qt::RightButton || mouseState == Qt::RightButton)
			previous_variation ();
		else
			previous_move ();
		m_cumulative_delta = 0;
	}

	// Delay of 100 msecs to avoid too fast scrolling
	wheelTime = QTime::currentTime();
	wheelTime = wheelTime.addMSecs(50);

	e->accept();
}

void Board::play_one_move (int x, int y)
{
	if (!player_to_move_p ())
		return;

	game_state *st = m_state;
	stone_color col = st->to_move ();

	game_state *st_new = st->add_child_move (x, y);
	if (st_new == nullptr)
		/* Invalid move.  */
		return;
	setModified();
	st->transfer_observers (st_new);
	m_board_win->player_move (col, x, y);
}

void Board::play_external_move (int x, int y)
{
	game_state *st = m_state;

	setModified();

	game_state *st_new = st->add_child_move (x, y);
	st->transfer_observers (st_new);
}

void Board::mouseReleaseEvent(QMouseEvent* e)
{
	mouseState = Qt::NoButton;

	if (m_mark_rect) {
		m_mark_rect = false;
		m_board_win->done_rect_select (m_rect_x1, m_rect_y1, m_rect_x2, m_rect_y2);
		return;
	}
	int x = convertCoordsToPoint (e->x (), m_board_rect.x ());
	int y = convertCoordsToPoint (e->y (), m_board_rect.y ());

	if (m_down_x == -1 || x != m_down_x || y != m_down_y)
		return;

	//qDebug("Mouse should be released after %d,%03d", wheelTime.second(),wheelTime.msec());
	//qDebug("Mouse released at time         %d,%03d", QTime::currentTime().second(),QTime::currentTime().msec());

	if (m_game_mode != modeMatch || QTime::currentTime() <= wheelTime)
		return;

	play_one_move (x - 1, y - 1);
}

void Board::mark_dead_external (int x, int y)
{
	m_edit_board->toggle_alive (x, y, false);
	/* There's evidence to suggest that the IGS algorithm at least has
	   no fancy tricks to find false eyes and such, and we should at
	   least try to match the final result that the server will
	   caclulate.
	   See also the modeScoreRemote case in the mouse event handler.  */
	m_edit_board->calc_scoring_markers_simple ();
	observed_changed ();
}

stone_color Board::swap_edit_to_move ()
{
	if (m_edit_board != nullptr)
		return m_edit_to_move = m_edit_to_move == black ? white : black;
	stone_color newcol = m_state->to_move () == black ? white : black;
	m_state->set_to_move (newcol);
	m_board_win->setMoveData (*m_state, m_state->get_board (), m_game_mode);
	return newcol;
}

void Board::click_add_mark (QMouseEvent *e, int x, int y)
{
	if (e->button () != Qt::RightButton && e->button () != Qt::LeftButton)
		return;

	mark mark_to_set = m_edit_mark;
	mextra mark_extra = 0;

	if (e->button () == Qt::RightButton)
		mark_to_set = mark::none;

	if (mark_to_set == mark::letter && e->modifiers () == Qt::ShiftModifier) {
		TextEditDialog dlg (m_board_win);
		dlg.textLineEdit->setFocus();
		const go_board &b = m_edit_board ? *m_edit_board : m_state->get_board ();

		if (b.mark_at (x, y) == mark::text)
			dlg.textLineEdit->setText (QString::fromStdString (b.mark_text_at (x, y)));

		if (dlg.exec() == QDialog::Accepted) {
			/* This is all a bit clunky; it's the price to pay for having game_state's get_board
			   return a const go_board.  */
			if (m_edit_board)
				m_edit_board->set_text_mark (x, y, dlg.textLineEdit->text().toStdString ());
			else
				m_state->set_text_mark (x, y, dlg.textLineEdit->text().toStdString ());
			setModified ();
			sync_appearance (true);
		}

		return;
	}
	if (mark_to_set == mark::num) {
		int i;
		for (i = 1; i < 256; i++)
			if (!m_used_numbers.test_bit (i))
				break;
		if (i == 256)
			return;
		mark_extra = i;
	} else if (m_edit_mark == mark::letter) {
		int i;
		for (i = 0; i < 52; i++)
			if (!m_used_letters.test_bit (i))
				break;
		if (i == 52)
			return;
		mark_extra = i;
	}

	bool changed;
	if (m_edit_board)
		changed = m_edit_board->set_mark (x, y, mark_to_set, mark_extra);
	else
		changed = m_state->set_mark (x, y, mark_to_set, mark_extra);
	if (changed) {
		setModified ();
		sync_appearance (true);
	}
}

void Board::mousePressEvent(QMouseEvent *e)
{
	mouseState = e->button();

	int x = convertCoordsToPoint (e->x (), m_board_rect.x ());
	int y = convertCoordsToPoint (e->y (), m_board_rect.y ());

	if (m_request_mark_rect && e->button () == Qt::LeftButton) {
		m_mark_rect = true;
		m_request_mark_rect = false;
		if (x < 1)
			x = 1;
		if (y < 1)
			y = 1;
		if (x > board_size)
			x = board_size;
		if (y > board_size)
			y = board_size;
		m_down_x = x;
		m_down_y = y;
		updateRectSel (x, y);
		return;
	}

	m_down_x = m_down_y = -1;

	if (x < 1 || x > board_size || y < 1 || y > board_size)
		return;

	m_down_x = x;
	m_down_y = y;

	if (navIntersectionStatus) {
		navIntersectionStatus = false;
		unsetCursor ();
		find_move (x - 1, y - 1);
		return;
	}
	if (m_eval_state != nullptr
	    && ((e->modifiers () == Qt::ShiftModifier && e->button () == Qt::LeftButton)
		|| e->button () == Qt::MiddleButton))
	{
		game_state *eval = m_eval_state->find_child_move (x - 1, y - 1);
		game_state *st = m_state;
		bool first = true;
		while (eval) {
			go_board b = st->get_board ();
			int tx = eval->get_move_x ();
			int ty = eval->get_move_y ();
			stone_color col = eval->get_move_color ();
			b.add_stone (tx, ty, col);
			st = st->add_child_move_nochecks (b, col, tx, ty, false);
			if (first) {
				QString comment = "Evaluation: W %1%2 B %3%4\n";
				int bp = b.bitpos (tx, ty);
				double wr = m_primary_eval + m_winrate[bp];
				double other_wr = 1 - wr;
				if (m_state->to_move () == black)
					std::swap (wr, other_wr);
				comment = (comment.arg (QString::number (100 * wr, 'f', 1)).arg ('%')
					   .arg (QString::number (100 * other_wr, 'f', 1)).arg ('%'));
				st->set_comment (comment.toStdString ());
			}
			eval = eval->next_move ();
			first = false;
		}
		if (!first) {
			sync_appearance ();
			m_board_win->update_game_tree (m_state);
			return;
		}
	}
	if (e->modifiers () == Qt::ControlModifier
	    && (m_game_mode == modeNormal || m_game_mode == modeObserve))
	{
		/* @@@ Previous code made a distinction between left and right button.  */
		find_move (x - 1, y - 1);
		return;
	}
	/* All modes where marks are not allowed, including scoring, force the editStone
	   button instead of the marks.  So this is a simple test.  */
	if (m_edit_mark != mark::none)
	{
		click_add_mark (e, x - 1, y - 1);
		return;
	}

	stone_color existing_stone;

	// resume normal proceeding
	switch (m_game_mode)
	{
	case modeNormal:
	case modeTeach: /* @@@ teaching mode is untested; best guess.  */
	case modeComputer:
		switch (e->button())
		{
		case Qt::LeftButton:
			play_one_move (x - 1, y - 1);
			break;

		default:
			break;
		}
		break;

	case modeEdit:
		existing_stone = m_edit_board->stone_at (x - 1, y - 1);
		switch (e->button())
		{
		case Qt::LeftButton:
			if (existing_stone == black)
				m_edit_board->set_stone (x - 1, y - 1, none);
			else
				m_edit_board->set_stone (x - 1, y - 1, black);
			setModified();
			sync_appearance (true);
			break;
		case Qt::RightButton:
			if (existing_stone == white)
				m_edit_board->set_stone (x - 1, y - 1, none);
			else
				m_edit_board->set_stone (x - 1, y - 1, white);
			setModified ();
			sync_appearance (true);
			break;

		default:
			break;
		}
		break;

	case modeScoreRemote:
		if (e->button () == Qt::LeftButton) {
			m_board_win->player_toggle_dead (x - 1, y - 1);
#if 0 /* We get our own toggle back from the server, at least in tests with NNGS.  */
			m_edit_board->toggle_alive (x - 1, y - 1);
			/* See comment in mark_dead_external.  */
			m_edit_board->calc_scoring_markers_simple ();
			observed_changed ();
#endif
		}
		break;
	case modeScore:
		switch (e->button())
		{
		case Qt::LeftButton:
			m_edit_board->toggle_alive (x - 1, y - 1);
			m_edit_board->calc_scoring_markers_complex ();
			observed_changed ();
			break;
		case Qt::RightButton:
			m_edit_board->toggle_seki (x - 1, y - 1);
			m_edit_board->calc_scoring_markers_complex ();
			observed_changed ();
			break;
		default:
			break;
		}
		break;

	case modeObserve:
		// do nothing but observe
		break;

	case modeMatch:
		// Delay of 250 msecs to avoid clickos
    		wheelTime = QTime::currentTime();
    		//qDebug("Mouse pressed at time %d,%03d", wheelTime.second(),wheelTime.msec());
		if (antiClicko)
			wheelTime = wheelTime.addMSecs(250);
		break;

	default:
		qWarning("   *** Invalid game mode! ***");
    }
}

void Board::changeSize()
{
#ifdef Q_OS_WIN
    resizeDelayFlag = false;
#endif
    resizeBoard(width(), height());
}

void Board::clear_stones ()
{
	size_t sz = m_stones.size ();
	for (size_t i = 0; i < sz; i++) {
		stone_gfx *s = m_stones[i];
		delete s;
		m_stones[i] = nullptr;
	}
}

void Board::clear_selection ()
{
	m_request_mark_rect = false;
	m_rect_x1 = m_rect_y1 = 1;
	m_rect_x2 = m_rect_y2 = m_game->boardsize ();
	updateCovers ();
}

void Board::reset_game (std::shared_ptr<game_record> gr)
{
	stop_observing ();

	delete m_grid;
	delete m_coords;

	game_state *root = gr->get_root ();

	const go_board &b = root->get_board ();
	int sz = b.size ();
	board_size = sz;

	clear_stones ();
	m_stones.resize (sz * sz);
	for (int i = 0; i < sz * sz; i++)
		m_stones[i] = nullptr;

	m_game = gr;
	clear_selection ();

	m_grid = new Grid (canvas, b, calculate_hoshis (b));
	m_coords = new CoordDisplay (canvas, b, coord_offset, coord_margin, showSGFCoords);

	calculateSize ();

	// Rescale the pixmaps in the ImageHandler
	imageHandler->rescale(square_size);

	// Redraw the board
	draw_background ();
	draw_grid_and_coords ();

	start_observing (root);

	canvas->update();
	setModified(false);
}

void Board::update_comment(const QString &qs)
{
	std::string s = qs.toStdString ();
	m_state->set_comment (s);
	setModified (true);
}

void Board::setShowCoords(bool b)
{
	bool old = showCoords;
	showCoords = b;
	if (old == showCoords)
		return;

	changeSize();
}

void Board::setShowSGFCoords(bool b)
{
	bool old = showSGFCoords;
	showSGFCoords = b;
	if (old == showSGFCoords)
		return;

	m_coords->set_texts (showSGFCoords);
}

void Board::set_vardisplay (bool children, int type)
{
	m_vars_children = children;
	m_vars_type = type;

	sync_appearance (true);
}

void Board::setModified(bool m)
{
	if (m == isModified || m_game_mode == modeObserve)
		return;

	isModified = m;
	m_board_win->updateCaption (isModified);
}

QPixmap Board::grabPicture()
{
	int sz = m_game->boardsize ();
	int minx = m_wood_rect.x () + 2;
	int miny = m_wood_rect.y () + 2;
	int maxx = minx + m_wood_rect.width () - 4;
	int maxy = miny + m_wood_rect.height () - 4;
	if (m_rect_x1 > 1)
		minx = coverLeft->rect ().right ();
	if (m_rect_x2 < sz)
		maxx = coverRight->rect ().left ();
	if (m_rect_y1 > 1)
		miny = coverTop->rect ().bottom ();
	if (m_rect_y2 < sz)
		maxy = coverBot->rect ().top ();
	return grab (QRect (minx, miny, maxx - minx, maxy - miny));
}

// button "Pass" clicked
void Board::doPass()
{
	if (!player_to_move_p ())
		return;
	if (m_game_mode == modeNormal || m_game_mode == modeComputer) {
		game_state *st = m_state->add_child_pass ();
		m_state->transfer_observers (st);
	}
}

void Board::play_external_pass ()
{
	game_state *st = m_state->add_child_pass ();
	m_state->transfer_observers (st);
}

void Board::navIntersection()
{
	setCursor(Qt::PointingHandCursor);
	navIntersectionStatus = true;
}

analyzer Board::analyzer_state ()
{
	if (m_analyzer == nullptr)
		return analyzer::disconnected;
	if (m_analyzer->stopped ())
		return analyzer::disconnected;
	if (!m_analyzer->started ())
		return analyzer::starting;
	if (m_pause_eval)
		return analyzer::paused;
	return analyzer::running;
}

void Board::setup_analyzer_position ()
{
	analyzer st = analyzer_state ();
	if (st != analyzer::running && st != analyzer::paused)
		return;
	std::vector<game_state *> moves;
	game_state *gst = m_state;
	while (gst->was_move_p () && !gst->root_node_p ()) {
		moves.push_back (gst);
		gst = gst->prev_move ();
	}
	const go_board &b = gst->get_board ();
	m_analyzer->clear_board ();
	for (int i = 0; i < board_size; i++)
		for (int j = 0; j < board_size; j++) {
			stone_color c = b.stone_at (i, j);
			if (c != none)
				m_analyzer->played_move (c, i, j);
		}
	while (!moves.empty ()) {
		gst = moves.back ();
		moves.pop_back ();
		m_analyzer->played_move (gst->get_move_color (),
					 gst->get_move_x (),
					 gst->get_move_y ());
	}
	clear_eval_data ();
	if (st == analyzer::running && !m_pause_eval) {
		stone_color to_move = m_state->to_move ();
		m_winrate = new double[board_size * board_size] ();
		m_visits = new int[board_size * board_size] ();
		m_analyzer->analyze (to_move, 100);
	}
}

void Board::gtp_startup_success ()
{
	m_board_win->update_analysis (analyzer::running);
	setup_analyzer_position ();
}

void Board::gtp_failure (const QString &err)
{
	clear_eval_data ();
	m_board_win->update_analysis (analyzer::disconnected);
	QMessageBox msg(QString (QObject::tr("Error")), err,
			QMessageBox::Warning, QMessageBox::Ok | QMessageBox::Default,
			Qt::NoButton, Qt::NoButton);
	msg.exec();
}

void Board::gtp_exited ()
{
	clear_eval_data ();
	m_board_win->update_analysis (analyzer::disconnected);
	QMessageBox::warning (this, PACKAGE, QObject::tr ("GTP process exited unexpectedly."));
}

void Board::gtp_eval (const QString &s)
{
	/* Right click pauses eval updates.  */
	if (mouseState == Qt::RightButton)
		return;

	bool prune = setting->readBoolEntry("ANALYSIS_PRUNE");

	QStringList moves = s.split ("info move ", QString::SkipEmptyParts);
	if (moves.isEmpty ())
		return;

	const go_board &b = m_state->get_board ();
	stone_color to_move = m_state->to_move ();
	delete m_eval_state;
	m_eval_state = new game_state (b, to_move);

	int an_maxmoves = setting->readIntEntry ("ANALYSIS_MAXMOVES");
	int count = 0;
	m_primary_eval = 0.5;
	for (auto &e: moves) {
//		m_board_win->append_comment (e);
		QRegExp re ("(\\S+)\\s+visits\\s+(\\d+)\\s+winrate\\s+(\\d+)\\s+prior\\s+(\\d+)\\s+order\\s+(\\d+)\\s+pv\\s+(.*)$");
		if (re.indexIn (e) == -1)
			continue;
		QString move = re.cap (1);
		int visits = re.cap (2).toInt ();
		int winrate = re.cap (3).toInt ();
		QString pv = re.cap (6);
		double wr = winrate / 10000.;

		if (count == 0) {
			m_primary_eval = wr;
			m_main_widget->set_eval (move, wr, m_state->to_move (), visits);
		}
		qDebug () << move << " PV: " << pv;

		QStringList pvmoves = pv.split (" ", QString::SkipEmptyParts);
		if (count < 52 && (!prune || pvmoves.length () > 1 || visits >= 2)) {
			game_state *cur = m_eval_state;
			bool pv_first = true;
			for (auto &pm: pvmoves) {
				QChar sx = pm[0];

				int i = sx.toLatin1 () - 'A';
				if (i > 7)
					i--;
				int j = board_size - pm.mid (1).toInt ();
				if (i >= 0 && i < board_size && j >= 0 && j < board_size) {
					if (pv_first) {
						int bp = b.bitpos (i, j);
						cur->set_mark (i, j, mark::letter, count);

						m_winrate[bp] = wr - m_primary_eval;
						m_visits[bp] = visits;
					}
					cur = cur->add_child_move (i, j);
				} else
					break;
				if (cur == nullptr)
					break;
				pv_first = false;
			}
		}
		count++;
		if (an_maxmoves > 0 && count == an_maxmoves)
			break;
	}
	sync_appearance ();
}

void Board::start_analysis ()
{
	Engine *e = client_window->analysis_engine ();
	if (e == nullptr) {
		QMessageBox::warning(this, PACKAGE, tr("You did not configure any analysis engine!"));
		return;
	}

	if (m_analyzer != nullptr) {
		m_analyzer->quit ();
		delete m_analyzer;
		m_analyzer = nullptr;
	}
	m_analyzer = create_gtp (*e, board_size, 7.5, 0);
	m_board_win->update_analysis (analyzer::starting);
}

void Board::stop_analysis ()
{
	clear_eval_data ();
	m_pause_eval = false;
	if (m_analyzer != nullptr && !m_analyzer->stopped ())
		m_analyzer->quit ();
	m_board_win->update_analysis (analyzer::disconnected);
}

void Board::pause_analysis (bool on)
{
	if (m_analyzer == nullptr || !m_analyzer->started () || m_analyzer->stopped ())
		return;
	m_pause_eval = on;
	if (on) {
		m_analyzer->pause_analysis ();
		m_board_win->update_analysis (analyzer::paused);
	} else {
		stone_color to_move = m_state->to_move ();
		m_winrate = new double[board_size * board_size] ();
		m_visits = new int[board_size * board_size] ();
		m_board_win->update_analysis (analyzer::running);
		m_analyzer->analyze (to_move, 100);
	}
}
