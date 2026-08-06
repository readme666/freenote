#include "Layout.h"

#include <algorithm>    // for max, lower_bound, transform
#include <cmath>        // for abs
#include <iterator>     // for begin, end, distance
#include <numeric>      // for accumulate
#include <optional>     // for optional
#include <type_traits>  // for make_signed_t, remove_referen...

#include <glib-object.h>  // for G_CALLBACK, g_signal_connect

#include "control/Control.h"            // for Control
#include "control/settings/Settings.h"  // for Settings
#include "gui/LayoutMapper.h"           // for LayoutMapper, GridPosition
#include "gui/PageView.h"               // for XojPageView
#include "gui/scroll/ScrollHandling.h"  // for ScrollHandling
#include "model/Document.h"             // for Document
#include "model/XojPage.h"              // for XojPage
#include "util/Range.h"                 // for Range
#include "util/Rectangle.h"             // for Rectangle
#include "util/safe_casts.h"            // for strict_cast, as_signed, as_si...

#include "XournalView.h"  // for XournalView

/**
 * Padding outside the pages, including shadow
 */
constexpr auto const XOURNAL_PADDING = 10;

/**
 * Allowance for shadow between page pairs in paired page mode
 */
constexpr auto const XOURNAL_ROOM_FOR_SHADOW = 3;

/**
 * Padding between the pages
 */
constexpr auto const XOURNAL_PADDING_BETWEEN = 15;


Layout::Layout(XournalView* view, ScrollHandling* scrollHandling): view(view), scrollHandling(scrollHandling) {
    g_signal_connect(scrollHandling->getHorizontal(), "value-changed", G_CALLBACK(horizontalScrollChanged), this);
    g_signal_connect(scrollHandling->getVertical(), "value-changed", G_CALLBACK(verticalScrollChanged), this);


    lastScrollHorizontal = gtk_adjustment_get_value(scrollHandling->getHorizontal());
    lastScrollVertical = gtk_adjustment_get_value(scrollHandling->getVertical());
}

void Layout::horizontalScrollChanged(GtkAdjustment* adjustment, Layout* layout) {
    const double newValue = gtk_adjustment_get_value(adjustment);
    const bool movingTowardsEnd = newValue > layout->lastScrollHorizontal + 0.5;
    layout->lastScrollHorizontal = newValue;
    if (!layout->blockHorizontalCallback) {
        if (movingTowardsEnd) {
            layout->maybeExpandInfiniteCanvas(true, false);
        }
        layout->updateVisibility();
        gtk_widget_queue_draw(layout->view->getWidget());
    }
}

void Layout::verticalScrollChanged(GtkAdjustment* adjustment, Layout* layout) {
    const double newValue = gtk_adjustment_get_value(adjustment);
    const bool movingTowardsEnd = newValue > layout->lastScrollVertical + 0.5;
    layout->lastScrollVertical = newValue;
    if (movingTowardsEnd) {
        layout->maybeExpandInfiniteCanvas(false, true);
    }
    layout->updateVisibility();
    layout->maybeAddLastPage(layout);
    gtk_widget_queue_draw(layout->view->getWidget());
}

auto Layout::hasInfiniteCanvas() const -> bool {
    const auto& pages = this->view->getViewPages();
    // Global reserve and no-centering are document-wide layout properties.
    // Keep the ordinary layout when an endless page is mixed with a fixed or
    // PDF page; individual endless pages still grow from input/content bounds.
    return !pages.empty() && std::all_of(pages.begin(), pages.end(),
                                         [](const auto& pageView) { return pageView->getPage()->isInfiniteCanvas(); });
}

void Layout::maybeExpandInfiniteCanvas(bool expandHorizontal, bool expandVertical) {
    if (this->expandingInfiniteCanvas) {
        return;
    }

    const auto visible = getVisibleRect();
    const double zoom = this->view->getZoom();
    if (zoom <= 0.0 || visible.width < 64.0 || visible.height < 64.0) {
        return;
    }

    const auto& views = this->view->getViewPages();
    const size_t selectedPage = this->view->getCurrentPage();
    for (size_t pageNo = 0; pageNo < views.size(); ++pageNo) {
        const auto& pageView = views[pageNo];
        const auto& page = pageView->getPage();
        // Scrolling past a non-final page means navigation to the following
        // page, not a request to keep pushing that page away.  Earlier endless
        // pages still grow when actual pen/mouse input approaches their edge.
        if (!page->isInfiniteCanvas() || pageNo != selectedPage || pageNo + 1 != views.size()) {
            continue;
        }

        const auto pagePos = getPixelCoordinatesOfEntry(pageNo);
        const double visibleRight = (visible.x + visible.width - pagePos.x) / zoom;
        const double visibleBottom = (visible.y + visible.height - pagePos.y) / zoom;
        const double viewportWidth = visible.width / zoom;
        const double viewportHeight = visible.height / zoom;

        // Ignore canvases which are wholly outside the viewport.  We still allow
        // the viewport to enter the right/bottom reserve belonging to this page.
        if (visibleRight < 0.0 || visibleBottom < 0.0 ||
            visible.x > pagePos.x + page->getWidth() * zoom + visible.width ||
            visible.y > pagePos.y + page->getHeight() * zoom + visible.height) {
            continue;
        }

        const double widthChunk = std::max(300.0, viewportWidth * 0.5);
        const double heightChunk = std::max(400.0, viewportHeight * 0.5);
        double newWidth = page->getWidth();
        double newHeight = page->getHeight();

        if (expandHorizontal && visibleRight >= page->getWidth() - viewportWidth * 0.25) {
            const double requested = std::max(page->getWidth(), visibleRight + viewportWidth * 0.5);
            newWidth = std::ceil(requested / widthChunk) * widthChunk;
        }
        if (expandVertical && visibleBottom >= page->getHeight() - viewportHeight * 0.25) {
            const double requested = std::max(page->getHeight(), visibleBottom + viewportHeight * 0.5);
            newHeight = std::ceil(requested / heightChunk) * heightChunk;
        }

        if (newWidth <= page->getWidth() && newHeight <= page->getHeight()) {
            continue;
        }

        auto* control = this->view->getControl();
        auto* document = control->getDocument();
        this->expandingInfiniteCanvas = true;
        document->lock();
        const size_t currentPageNo = document->indexOf(page);
        if (currentPageNo != npos) {
            page->setSize(newWidth, newHeight);
        }
        document->unlock();
        if (currentPageNo != npos) {
            control->firePageSizeChanged(currentPageNo);
        }
        this->expandingInfiniteCanvas = false;
        return;  // The relayout invalidated all cached page positions.
    }
}

void Layout::maybeAddLastPage(Layout* layout) {
    auto* control = this->view->getControl();
    auto* settings = control->getSettings();
    const auto& currentPage = control->getCurrentPage();
    if (currentPage && currentPage->isInfiniteCanvas()) {
        // The endless page itself supplies more space; automatic page append
        // would turn every scroll-to-edge into an unintended finite page.
        return;
    }
    if (settings->getEmptyLastPageAppend() == EmptyLastPageAppendType::OnScrollToEndOfLastPage) {
        // If the layout is 5px away from the end of the last page
        if (std::abs((layout->getTotalPixelHeight() - layout->getVisibleRect().y) - layout->getVisibleRect().height) <
            5) {
            auto* doc = control->getDocument();
            doc->lock_shared();
            auto pdfPageCount = doc->getPdfPageCount();
            auto lastPage = doc->getPageCount() - 1;
            doc->unlock_shared();
            if (pdfPageCount == 0) {
                auto currentPage = control->getCurrentPageNo();
                if (currentPage == lastPage) {
                    control->insertNewPage(currentPage + 1, true);
                }
            }
        }
    }
}

/**
 * Iteratable view that computes the pixel coordinates of the delimitation between columns (makeHorizontal) or rows
 * (makeVertical) in the layout from the PreCalculated zoom-agnostic data
 *
 * The value returned by a given iterator is the pixel coordinate AFTER the corresponding column/row.
 */
struct Layout::PixelCounter {
    static PixelCounter makeVertical(const PreCalculated& pc, double zoom) {
        return {pc.stretchableVerticalPixelsAfterRow, pc.paddingTop + pc.verticalCenteringPadding, zoom};
    }
    static PixelCounter makeHorizontal(const PreCalculated& pc, double zoom) {
        return {pc.stretchableHorizontalPixelsAfterColumn, pc.paddingLeft + pc.horizontalCenteringPadding, zoom};
    }

    static_assert(std::is_same<decltype(PreCalculated::stretchableHorizontalPixelsAfterColumn),
                               decltype(PreCalculated::stretchableVerticalPixelsAfterRow)>());
    using base_container = decltype(PreCalculated::stretchableHorizontalPixelsAfterColumn);
    using base_iterator = base_container::const_iterator;

private:
    PixelCounter(const base_container& c, int padding, double zoom):
            b(c.begin()), e(c.end()), padding(padding), zoom(zoom) {}

    base_iterator b;
    base_iterator e;
    int padding;
    double zoom;

public:
    struct iterator {
        iterator() = default;
        iterator(const iterator& o) = default;
        iterator(iterator&& o) = default;
        iterator& operator=(const iterator& o) = default;
        iterator& operator=(iterator&& o) = default;

        using difference_type = base_iterator::difference_type;
        using iterator_category = std::random_access_iterator_tag;
        using value_type = double;
        using reference = double;
        using pointer = void;

        iterator(base_iterator base, const PixelCounter* parent): it(base), parent(parent) {}


        /**
         * This is the only function with non-trivial content of this iterator class. Computes the pixel position based
         * on the precalculated zoom-agnostic data
         */
        value_type operator*() const {
            return *it * parent->zoom + parent->padding +
                   XOURNAL_PADDING_BETWEEN * static_cast<double>(std::distance(parent->b, it)) +
                   .5 * XOURNAL_PADDING_BETWEEN;
        }

        // input iterator
        bool operator==(const iterator& o) const { return it == o.it; }
        bool operator!=(const iterator& o) const { return it != o.it; }
        iterator& operator++() {
            ++it;
            return *this;
        }

        // forward iterator
        iterator operator++(int) {
            auto c = *this;
            this->operator++();
            return c;
        }

        // bidirectional operator
        iterator& operator--() {
            --it;
            return *this;
        }
        iterator operator--(int) {
            auto c = *this;
            this->operator--();
            return c;
        }

        // random access iterator
        iterator& operator+=(difference_type n) {
            it += n;
            return *this;
        }
        iterator operator+(difference_type n) const { return iterator(it + n, parent); }
        friend iterator operator+(iterator::difference_type n, const iterator& self) { return self + n; }
        iterator& operator-=(difference_type n) {
            it -= n;
            return *this;
        }
        iterator operator-(difference_type n) const { return iterator(it - n, parent); }
        difference_type operator-(const iterator& o) const { return it - o.it; }
        value_type operator[](difference_type n) const { return *(*this + n); }
        bool operator<(const iterator& o) const { return it < o.it; }
        bool operator<=(const iterator& o) const { return it <= o.it; }
        bool operator>(const iterator& o) const { return it > o.it; }
        bool operator>=(const iterator& o) const { return it >= o.it; }

    private:
        base_iterator it;
        const PixelCounter* parent;
    };
    iterator begin() const { return iterator(b, this); }
    iterator end() const { return iterator(e, this); }
};

void Layout::forEachEntriesIntersectingRange(
        const Range& rg, std::function<void(size_t, const Range&, xoj::util::Point<int>)> fun) const {
    if (rg.empty()) {
        return;
    }
    xoj_assert(rg.isValid());
    const double zoom = this->view->getZoom();

    std::lock_guard g{pc.m};
    auto verticalPixels = PixelCounter::makeVertical(this->pc, zoom);
    auto horizontalPixels = PixelCounter::makeHorizontal(this->pc, zoom);

    // Use binary search to find the possibly visible slots
    auto xEnd = [&]() {
        // To get an actual "end", take the next one
        auto it = std::upper_bound(horizontalPixels.begin(), horizontalPixels.end(), rg.maxX);
        return it == horizontalPixels.end() ? horizontalPixels.end() : std::next(it);
    }();
    auto xBegin = std::lower_bound(horizontalPixels.begin(), xEnd, rg.minX);

    auto yEnd = [&]() {
        // To get an actual "end", take the next one
        auto it = std::upper_bound(verticalPixels.begin(), verticalPixels.end(), rg.maxY);
        return it == verticalPixels.end() ? verticalPixels.end() : std::next(it);
    }();
    auto yBegin = std::lower_bound(verticalPixels.begin(), yEnd, rg.minY);

    for (auto col = as_unsigned(std::distance(horizontalPixels.begin(), xBegin)),
              endCol = as_unsigned(std::distance(horizontalPixels.begin(), xEnd));
         col != endCol; col++) {
        for (auto row = as_unsigned(std::distance(verticalPixels.begin(), yBegin)),
                  endRow = as_unsigned(std::distance(verticalPixels.begin(), yEnd));
             row != endRow; row++) {
            auto optionalPage = pc.mapper.at({as_unsigned(col), as_unsigned(row)});
            if (optionalPage) {
                auto pos = this->getPixelCoordinatesOfEntryUnsafe(optionalPage.value());
                auto& pageView = this->view->getViewPages()[optionalPage.value()];
                double w = pageView->getWidth();
                double h = pageView->getHeight();
                Range pageRg(pos.x, pos.y, pos.x + w * zoom, pos.y + h * zoom);
                if (auto intersection = pageRg.intersect(rg); !intersection.empty()) {
                    fun(optionalPage.value(), intersection, pos);
                }
            }
        }
    }
}

void Layout::updateVisibility() {
    auto visibleRg = Range(getVisibleRect());
    xoj::util::Point<int> center(round_cast<int>(.5 * (visibleRg.minX + visibleRg.maxX)),
                                 round_cast<int>(.5 * (visibleRg.minY + visibleRg.maxY)));

    // Data to select page based on visibility
    std::optional<size_t> mostPageNr;
    double mostPagePercent = 0;

    std::vector<size_t> visiblePages;
    forEachEntriesIntersectingRange(visibleRg, [&](size_t index, const Range& intersection, xoj::util::Point<int> pos) {
        auto& pageView = this->view->getViewPages()[index];
        pageView->setIsVisible(true);
        visiblePages.emplace_back(index);

        // Set the selected page
        double percent =
                intersection.getWidth() * intersection.getHeight() / (visibleRg.getWidth() * visibleRg.getHeight());

        if (percent > mostPagePercent) {
            mostPageNr = index;
            mostPagePercent = percent;
        }
    });

    std::sort(visiblePages.begin(), visiblePages.end());
    xoj_assert(std::is_sorted(this->previouslyVisiblePages.begin(), this->previouslyVisiblePages.end()));

    auto it = visiblePages.begin();
    for (auto&& s: this->previouslyVisiblePages) {
        xoj_assert(s < this->view->getViewPages().size());
        while (it != visiblePages.end() && *it < s) {
            it++;
        }
        if (it == visiblePages.end() || *it != s) {
            // This page is no longer visible
            this->view->getViewPages()[s]->setIsVisible(false);
        }
    }
    this->previouslyVisiblePages = std::move(visiblePages);
    if (mostPageNr) {
        this->view->getControl()->firePageSelected(*mostPageNr);
    }
}

auto Layout::getVisiblePages() const -> std::vector<size_t> {
    // We make a copy in case previouslyVisiblePages's iterators get invalidated. The vector is typically very small.
    return previouslyVisiblePages;
}

auto Layout::getVisibleRect() -> xoj::util::Rectangle<double> {
    return xoj::util::Rectangle<double>(gtk_adjustment_get_value(scrollHandling->getHorizontal()),
                                        gtk_adjustment_get_value(scrollHandling->getVertical()),
                                        gtk_adjustment_get_page_size(scrollHandling->getHorizontal()),
                                        gtk_adjustment_get_page_size(scrollHandling->getVertical()));
}

void Layout::computePrecalculated() {
    auto len = view->getViewPages().size();
    pc.mapper.configureFromSettings(len, view->getControl()->getSettings());
    auto colCount = pc.mapper.getColumns();
    auto rowCount = pc.mapper.getRows();

    pc.widthCols.assign(colCount, 0);
    pc.heightRows.assign(rowCount, 0);

    // When we add/remove a page, the indices in previouslyVisiblePages are invalidated
    previouslyVisiblePages.clear();

    for (size_t pageIdx{}; pageIdx < len; ++pageIdx) {
        auto const& raster_p = pc.mapper.at(pageIdx);  // auto [c, r] raster = mapper.at();
        auto const& c = raster_p.col;
        auto const& r = raster_p.row;
        auto& v = view->viewPages[pageIdx];
        pc.widthCols[c] = std::max(pc.widthCols[c], v->getWidth());
        pc.heightRows[r] = std::max(pc.heightRows[r], v->getHeight());
        v->setGridCoordinates({strict_cast<int>(c), strict_cast<int>(r)});
        if (v->isVisible()) {
            // The page may no longer be visible after the relayout. That will be handled in updateVisibility() later
            previouslyVisiblePages.emplace_back(pageIdx);
        }
    }

    pc.stretchableHorizontalPixelsAfterColumn.clear();
    pc.stretchableHorizontalPixelsAfterColumn.reserve(colCount);
    pc.stretchableVerticalPixelsAfterRow.clear();
    pc.stretchableVerticalPixelsAfterRow.reserve(rowCount);

    std::partial_sum(pc.widthCols.begin(), pc.widthCols.end(),
                     std::back_inserter(pc.stretchableHorizontalPixelsAfterColumn));
    std::partial_sum(pc.heightRows.begin(), pc.heightRows.end(),
                     std::back_inserter(pc.stretchableVerticalPixelsAfterRow));

    auto* settings = view->getControl()->getSettings();
    pc.paddingTop = XOURNAL_PADDING;
    pc.paddingBottom = XOURNAL_PADDING;
    if (settings->getUnlimitedScrolling()) {
        pc.paddingTop += round_cast<int>(gtk_adjustment_get_page_size(scrollHandling->getVertical()));
        pc.paddingBottom = pc.paddingTop;
    } else if (settings->getAddVerticalSpace()) {
        pc.paddingTop += settings->getAddVerticalSpaceAmountAbove();
        pc.paddingBottom += settings->getAddVerticalSpaceAmountBelow();
    }
    if (hasInfiniteCanvas()) {
        pc.paddingBottom = std::max(
                pc.paddingBottom,
                XOURNAL_PADDING + round_cast<int>(gtk_adjustment_get_page_size(scrollHandling->getVertical())));
    }

    pc.paddingLeft = XOURNAL_PADDING;
    pc.paddingRight = XOURNAL_PADDING;
    if (settings->getUnlimitedScrolling()) {
        pc.paddingLeft += round_cast<int>(gtk_adjustment_get_page_size(scrollHandling->getHorizontal()));
        pc.paddingRight = pc.paddingLeft;
    } else if (settings->getAddHorizontalSpace()) {
        pc.paddingLeft += settings->getAddHorizontalSpaceAmountLeft();
        pc.paddingRight += settings->getAddHorizontalSpaceAmountRight();
    }
    if (hasInfiniteCanvas()) {
        pc.paddingRight = std::max(
                pc.paddingRight,
                XOURNAL_PADDING + round_cast<int>(gtk_adjustment_get_page_size(scrollHandling->getHorizontal())));
    }

    recomputeCenteringPaddingUnsafe(gtk_widget_get_allocated_width(view->getWidget()),
                                    gtk_widget_get_allocated_height(view->getWidget()));
}

void Layout::recomputeCenteringPaddingUnsafe(int allocWidth, int allocHeight) {
    if (hasInfiniteCanvas()) {
        pc.horizontalCenteringPadding = 0;
        pc.verticalCenteringPadding = 0;
        return;
    }
    if (int w = getMinimalPixelWidthUnsafe(); w < allocWidth) {
        // We have more space than needed: add padding to center the content
        pc.horizontalCenteringPadding = (allocWidth - w) / 2;
    } else {
        pc.horizontalCenteringPadding = 0;
    }

    if (int h = getMinimalPixelHeightUnsafe(); h < allocHeight) {
        // We have more space than needed: add padding to center the content
        pc.verticalCenteringPadding = (allocHeight - h) / 2;
    } else {
        pc.verticalCenteringPadding = 0;
    }
}

void Layout::recomputeCenteringPadding(int allocWidth, int allocHeight) {
    if (allocWidth == -1 || allocHeight == -1) {
        allocWidth = round_cast<int>(gtk_adjustment_get_page_size(scrollHandling->getHorizontal()));
        allocHeight = round_cast<int>(gtk_adjustment_get_page_size(scrollHandling->getVertical()));
    }
    std::unique_lock g{pc.m};
    recomputeCenteringPaddingUnsafe(allocWidth, allocHeight);
    auto width = getTotalPixelWidthUnsafe();
    auto height = getTotalPixelHeightUnsafe();
    g.unlock();
    gtk_adjustment_set_upper(scrollHandling->getHorizontal(), width);
    gtk_adjustment_set_upper(scrollHandling->getVertical(), height);
}

void Layout::recalculate() {
    computePrecalculated();
    gtk_adjustment_set_upper(scrollHandling->getHorizontal(), getTotalPixelWidth());
    gtk_adjustment_set_upper(scrollHandling->getVertical(), getTotalPixelHeight());
    gtk_widget_queue_draw(view->getWidget());
}

auto Layout::getFixedPaddingBeforePoint(const xoj::util::Point<double>& ref) const -> xoj::util::Point<int> {
    std::lock_guard g{pc.m};
    auto pos = getGridPositionAtUnsafe(ref);
    int paddingY = strict_cast<int>(pos.row) * XOURNAL_PADDING_BETWEEN + pc.paddingTop;
    int paddingX =
            [&](int col) {
                if (!pc.mapper.isPairedPages()) {
                    // No page pairing or we haven't rendered enough pages in the row for page pairing to have an effect
                    return col * XOURNAL_PADDING_BETWEEN;
                } else {
                    auto columnPadding = XOURNAL_PADDING_BETWEEN + col * XOURNAL_PADDING_BETWEEN;
                    if (col % 2 == 0) {
                        return columnPadding - XOURNAL_ROOM_FOR_SHADOW;
                    } else {
                        return columnPadding + XOURNAL_ROOM_FOR_SHADOW;
                    }
                }
            }(strict_cast<int>(pos.col)) +
            pc.paddingLeft;
    return {paddingX, paddingY};
}

auto Layout::getCenteringPadding() const -> xoj::util::Point<int> {
    std::lock_guard g{pc.m};
    return {pc.horizontalCenteringPadding, pc.verticalCenteringPadding};
}

void Layout::scrollRelative(double x, double y) {
    scrollAbs(gtk_adjustment_get_value(scrollHandling->getHorizontal()) + x,
              gtk_adjustment_get_value(scrollHandling->getVertical()) + y);
}

void Layout::scrollAbs(double x, double y) {
    if (this->view->getControl()->getSettings()->isPresentationMode()) {
        return;
    }

    // We block the horizontal callback to avoid calling updateVisibility() twice
    this->blockHorizontalCallback = true;
    gtk_adjustment_set_value(scrollHandling->getHorizontal(), x);
    gtk_adjustment_set_value(scrollHandling->getVertical(), y);
    this->blockHorizontalCallback = false;
}

void Layout::ensureRectIsVisible(int x, int y, int width, int height) {
    // We block the horizontal callback to avoid calling updateVisibility() twice
    this->blockHorizontalCallback = true;
    gtk_adjustment_clamp_page(scrollHandling->getHorizontal(), x - 5, x + width + 10);
    gtk_adjustment_clamp_page(scrollHandling->getVertical(), y - 5, y + height + 10);
    this->blockHorizontalCallback = false;
}

auto Layout::getGridPositionAtUnsafe(const xoj::util::Point<double>& p) const -> GridPosition {
    // We do a binary search to find the grid position
    double zoom = this->view->getZoom();
    auto verticalPixels = PixelCounter::makeVertical(this->pc, zoom);
    auto rit = std::lower_bound(verticalPixels.begin(), verticalPixels.end(), p.y);
    auto const foundRow = strict_cast<size_t>(std::distance(verticalPixels.begin(), rit));

    auto horizontalPixels = PixelCounter::makeHorizontal(this->pc, zoom);
    auto cit = std::lower_bound(horizontalPixels.begin(), horizontalPixels.end(), p.x);
    auto const foundCol = strict_cast<size_t>(std::distance(horizontalPixels.begin(), cit));
    return GridPosition{foundCol, foundRow};
}

auto Layout::getPageViewAt(int x, int y) const -> XojPageView* {
    auto optionalPage = [&]() {
        std::lock_guard g{pc.m};
        return pc.mapper.at(getGridPositionAtUnsafe(xoj::util::Point<double>(x, y)));
    }();

    if (optionalPage && this->view->getViewPages()[*optionalPage]->containsPoint(x, y, false)) {
        return this->view->getViewPages()[*optionalPage].get();
    }

    // The right/bottom reserve is part of an endless page for input purposes.
    // The page will acquire a finite stored extent before the input is committed.
    const auto& pages = this->view->getViewPages();
    const int horizontalReserve = round_cast<int>(gtk_adjustment_get_page_size(scrollHandling->getHorizontal()));
    const int verticalReserve = round_cast<int>(gtk_adjustment_get_page_size(scrollHandling->getVertical()));
    for (size_t pageNo = 0; pageNo < pages.size(); ++pageNo) {
        const auto& pageView = pages[pageNo];
        if (!pageView->getPage()->isInfiniteCanvas()) {
            continue;
        }

        const auto pos = getPixelCoordinatesOfEntry(pageNo);
        const int right = pos.x + pageView->getDisplayWidth();
        const int bottom = pos.y + pageView->getDisplayHeight();
        const bool isLastPage = pageNo + 1 == pages.size();
        const int extendedBottom = bottom + (isLastPage ? verticalReserve : 0);
        const bool inReserve = x >= pos.x && x <= right + horizontalReserve && y >= pos.y && y <= extendedBottom;
        if (inReserve) {
            return pageView.get();
        }
    }

    return nullptr;
}

auto Layout::getPageWithRelativePosition(size_t referencePageNumber, int columnOffset, int rowOffset) const
        -> std::optional<size_t> {
    auto pos = pc.mapper.at(referencePageNumber);
    return pc.mapper.at({as_unsigned(as_signed(pos.col) + columnOffset), as_unsigned(as_signed(pos.row) + rowOffset)});
}

auto Layout::getTotalPixelHeight() const -> int {
    std::lock_guard g{pc.m};
    return getTotalPixelHeightUnsafe();
}

auto Layout::getTotalPixelHeightUnsafe() const -> int {
    return getMinimalPixelHeightUnsafe() + 2 * pc.verticalCenteringPadding;
}

auto Layout::getMinimalPixelHeight() const -> int {
    std::lock_guard g{pc.m};
    return getMinimalPixelHeightUnsafe();
}

auto Layout::getMinimalPixelHeightUnsafe() const -> int {
    auto rowCount = pc.mapper.getRows();
    return pc.paddingTop + pc.paddingBottom + strict_cast<int>(rowCount - 1) * XOURNAL_PADDING_BETWEEN +
           ceil_cast<int>(pc.stretchableVerticalPixelsAfterRow.back() * view->getZoom());
}

auto Layout::getTotalPixelWidth() const -> int {
    std::lock_guard g{pc.m};
    return getTotalPixelWidthUnsafe();
}

auto Layout::getTotalPixelWidthUnsafe() const -> int {
    return getMinimalPixelWidthUnsafe() + 2 * pc.horizontalCenteringPadding;
}

auto Layout::getMinimalPixelWidth() const -> int {
    std::lock_guard g{pc.m};
    return getMinimalPixelWidthUnsafe();
}

auto Layout::getMinimalPixelWidthUnsafe() const -> int {
    auto colCount = pc.mapper.getColumns();
    return pc.paddingLeft + pc.paddingRight + strict_cast<int>(colCount - 1) * XOURNAL_PADDING_BETWEEN +
           ceil_cast<int>(pc.stretchableHorizontalPixelsAfterColumn.back() * view->getZoom());
}

auto Layout::getPixelCoordinatesOfEntry(xoj::util::Point<int> gridCoords) const -> xoj::util::Point<int> {
    std::lock_guard g{pc.m};
    return getPixelCoordinatesOfEntryUnsafe(gridCoords);
}

auto Layout::getPixelCoordinatesOfEntry(size_t n) const -> xoj::util::Point<int> {
    std::lock_guard g{pc.m};
    return getPixelCoordinatesOfEntryUnsafe(n);
}

static xoj::util::Point<int> getPixelCoords(const Layout::PreCalculated& pc, const GridPosition& pos, XojPageView& pv) {
    double zoom = pv.getZoom();
    return {floor_cast<int>(((pos.col == 0 ? 0. : pc.stretchableHorizontalPixelsAfterColumn[pos.col - 1]) +
                             .5 * (pc.widthCols[pos.col] - pv.getWidth())) *
                            zoom) +
                    strict_cast<int>(pos.col) * XOURNAL_PADDING_BETWEEN + pc.paddingLeft +
                    pc.horizontalCenteringPadding,
            floor_cast<int>(((pos.row == 0 ? 0. : pc.stretchableVerticalPixelsAfterRow[pos.row - 1]) +
                             .5 * (pc.heightRows[pos.row] - pv.getHeight())) *
                            zoom) +
                    strict_cast<int>(pos.row) * XOURNAL_PADDING_BETWEEN + pc.paddingTop + pc.verticalCenteringPadding};
}

auto Layout::getPixelCoordinatesOfEntryUnsafe(xoj::util::Point<int> gridCoords) const -> xoj::util::Point<int> {
    GridPosition pos{size_t(gridCoords.x), size_t(gridCoords.y)};

    auto optionalPage = pc.mapper.at(pos);

    if (optionalPage) {
        return getPixelCoords(this->pc, pos, *this->view->getViewPages()[*optionalPage]);
    } else {
        return {0, 0};
    }
}

auto Layout::getPixelCoordinatesOfEntryUnsafe(size_t n) const -> xoj::util::Point<int> {
    return getPixelCoords(this->pc, pc.mapper.at(n), *this->view->getViewPages()[n]);
}
