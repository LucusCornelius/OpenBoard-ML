/*
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */

#include "UBMathPalette.h"

#include <QButtonGroup>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QStackedWidget>
#include <QTextDocument>
#include <QTextTable>
#include <QToolButton>
#include <QVBoxLayout>

#include "board/UBBoardController.h"
#include "board/UBBoardView.h"
#include "board/UBDrawingController.h"
#include "core/UBApplication.h"
#include "core/UBSettings.h"
#include "domain/UBGraphicsScene.h"
#include "domain/UBGraphicsTextItem.h"

#include "core/memcheck.h"

namespace
{
    // markup of the stacked fraction, which is built as a two-row table instead of inline text
    const QString sStackedFractionMarkup = QStringLiteral("@fraction");

    const int sColumns = 10;
    const int sButtonWidth = 40;
    const int sButtonHeight = 34;

    // Characters after which an "optional base" (e.g. the x of x²) is still inserted,
    // because there is no operand directly before the cursor.
    const QString sNoBaseBefore = QStringLiteral(" \t(=+-−*×·/÷,;:[{<>≤≥≈≠→⇒⇔");
}

UBMathPalette::UBMathPalette(QWidget* parent)
    : UBFloatingPalette(Qt::TopLeftCorner, parent)
    , mPageButtons(nullptr)
    , mPages(nullptr)
    , mHasPreferredPos(false)
{
    setObjectName("MathPalette");
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);

    // the palette keeps the position where the user dropped it
    setCustomPosition(true);

    setStyleSheet(
        "QToolButton#MathPaletteButton {"
        "  background-color: rgba(255, 255, 255, 220); color: #202020;"
        "  border: 1px solid #8f8f8f; border-radius: 5px; padding: 0px 3px; }"
        "QToolButton#MathPaletteButton:hover { background-color: #ffffff; border-color: #2f6fd6; }"
        "QToolButton#MathPaletteButton:pressed { background-color: #cfdcf5; }"
        "QToolButton#MathPaletteTab {"
        "  color: #202020; background: transparent; border: none;"
        "  border-bottom: 2px solid transparent; padding: 3px 7px; }"
        "QToolButton#MathPaletteTab:hover { background-color: rgba(255, 255, 255, 110); }"
        "QToolButton#MathPaletteTab:checked { border-bottom: 2px solid #2f6fd6; font-weight: bold; }");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(sLayoutContentMargin, sLayoutContentMargin / 2 + 2,
                                   sLayoutContentMargin, sLayoutContentMargin);
    mainLayout->setSpacing(6);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(2);
    // room for the close button of the floating palette (top left corner)
    headerLayout->addSpacing(closeButtonMargin() + 28 - sLayoutContentMargin + 4);

    mPageButtons = new QButtonGroup(this);
    mPageButtons->setExclusive(true);
    mPages = new QStackedWidget(this);
    mPages->setFocusPolicy(Qt::NoFocus);

    const QList<MathPage> pages = buildPages();

    for (int p = 0; p < pages.size(); ++p)
    {
        const MathPage& page = pages.at(p);

        QToolButton* tab = new QToolButton(this);
        tab->setObjectName("MathPaletteTab");
        tab->setText(page.title);
        tab->setCheckable(true);
        tab->setFocusPolicy(Qt::NoFocus);
        tab->setCursor(Qt::PointingHandCursor);
        mPageButtons->addButton(tab, p);
        headerLayout->addWidget(tab);

        QWidget* pageWidget = new QWidget(mPages);
        pageWidget->setFocusPolicy(Qt::NoFocus);
        QGridLayout* grid = new QGridLayout(pageWidget);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(4);
        grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        for (int i = 0; i < page.templates.size(); ++i)
        {
            QToolButton* button = createTemplateButton(page.templates.at(i), pageWidget);
            grid->addWidget(button, i / sColumns, i % sColumns);
        }

        mPages->addWidget(pageWidget);
    }

    headerLayout->addStretch(1);

    QToolButton* normalButton = new QToolButton(this);
    normalButton->setObjectName("MathPaletteButton");
    normalButton->setText(tr("Normal"));
    normalButton->setToolTip(tr("Continue in normal text: leave superscript, subscript or overline\n"
                                "(with a selection: make the selected text normal)"));
    normalButton->setFocusPolicy(Qt::NoFocus);
    normalButton->setFixedHeight(sButtonHeight - 6);
    connect(normalButton, &QToolButton::clicked, this, &UBMathPalette::normalTextClicked);
    headerLayout->addWidget(normalButton);

    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(mPages);

    connect(mPageButtons, &QButtonGroup::idClicked, this, &UBMathPalette::pageSelected);

    int page = UBSettings::settings()->boardMathPaletteTab->get().toInt();

    if (page < 0 || page >= mPages->count())
    {
        page = 0;
    }

    mPageButtons->button(page)->setChecked(true);
    mPages->setCurrentIndex(page);
    updatePageSize();

    const QVariant position = UBSettings::settings()->boardMathPalettePosition->get();

    if (position.isValid() && position.canConvert<QPoint>())
    {
        mPreferredPos = position.toPoint();
        mHasPreferredPos = true;
    }

    // build the close button last so that it is on top of the layout
    setClosable(true);
    resize(sizeHint());

    if (parent)
    {
        parent->installEventFilter(this);
    }
}

UBMathPalette::~UBMathPalette()
{
    // NOOP
}

QString UBMathPalette::mathFontFamily()
{
    static bool initialized = false;
    static QString family;

    if (!initialized)
    {
        initialized = true;

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        const QStringList available = QFontDatabase::families();
#else
        const QStringList available = QFontDatabase().families();
#endif
        const QStringList candidates = {
            "Cambria Math", "Cambria", "STIX Two Math", "STIX Two Text", "STIXGeneral",
            "DejaVu Serif", "Noto Sans Math"
        };

        for (const QString& candidate : candidates)
        {
            if (!available.contains(candidate, Qt::CaseInsensitive))
            {
                continue;
            }

            // Some maths fonts declare a huge line spacing (to make room for big operators),
            // which would make every line of the text item very tall. Skip those.
            QFont font(candidate);
            font.setPixelSize(100);
            QFontMetricsF metrics(font);

            if (metrics.height() > 160.)
            {
                continue;
            }

            family = candidate;
            break;
        }
    }

    return family;
}

QList<UBMathPalette::MathPage> UBMathPalette::buildPages() const
{
    QList<MathPage> pages;

    // the label is shown on the button, the markup is inserted (see class documentation)
    MathPage calculus{tr("Calculus"), {
        {QString::fromUtf8("√x̅"), tr("Square root"), QString::fromUtf8("√~{[x]} "), false},
        {QString::fromUtf8("ⁿ√x̅"), tr("n-th root"), QString::fromUtf8("^{[n]}√~{x} "), false},
        {QString::fromUtf8("∫"), tr("Integral"), QString::fromUtf8("∫"), false},
        {QString::fromUtf8("∫ₐᵇ"), tr("Definite integral with bounds: ∫ f(x) dx from a to b"), QString::fromUtf8("∫_{[a]}^{b} f(x) dx"), false},
        {QString::fromUtf8("∬"), tr("Double integral"), QString::fromUtf8("∬"), false},
        {QString::fromUtf8("∮"), tr("Contour integral"), QString::fromUtf8("∮"), false},
        {QString::fromUtf8("[F]ₐᵇ"), tr("Evaluate a primitive between bounds: [F(x)] from a to b"), QString::fromUtf8("\\[[F(x)]\\]_{a}^{b}"), false},
        {QString::fromUtf8("∑ᵢⁿ"), tr("Sum with bounds"), QString::fromUtf8("∑_{[i=1]}^{n} "), false},
        {QString::fromUtf8("∏ᵢⁿ"), tr("Product with bounds"), QString::fromUtf8("∏_{[i=1]}^{n} "), false},
        {QString::fromUtf8("lim"), tr("Limit: lim x → a"), QString::fromUtf8("lim_{[x→a]} "), false},
        {QString::fromUtf8("d⁄dx"), tr("Derivative d/dx"), QString::fromUtf8("^{d}⁄_{d[x]} "), false},
        {QString::fromUtf8("∂⁄∂x"), tr("Partial derivative ∂/∂x"), QString::fromUtf8("^{∂}⁄_{∂[x]} "), false},
        {QString::fromUtf8("f′(x)"), tr("Derivative f′(x)"), QString::fromUtf8("f′([x])"), false},
        {QString::fromUtf8("f″(x)"), tr("Second derivative f″(x)"), QString::fromUtf8("f″([x])"), false},
        {QString::fromUtf8("dx"), tr("dx"), QString::fromUtf8("dx"), false},
        {QString::fromUtf8("dy"), tr("dy"), QString::fromUtf8("dy"), false},
        {QString::fromUtf8("dt"), tr("dt"), QString::fromUtf8("dt"), false},
        {QString::fromUtf8("∞"), tr("Infinity"), QString::fromUtf8("∞"), false},
        {QString::fromUtf8("Δ"), tr("Delta (difference)"), QString::fromUtf8("Δ"), false},
        {QString::fromUtf8("→"), tr("Tends to"), QString::fromUtf8("→"), false}
    }};

    MathPage algebra{tr("Algebra"), {
        {QString::fromUtf8("( )"), tr("Parentheses"), QString::fromUtf8("(|)"), false},
        {QString::fromUtf8("[ ]"), tr("Square brackets"), QString::fromUtf8("\\[|\\]"), false},
        {QString::fromUtf8("{ }"), tr("Braces"), QString::fromUtf8("\\{|\\}"), false},
        {QString::fromUtf8("| |"), tr("Absolute value"), QString::fromUtf8("\\||\\|"), false},
        {QString::fromUtf8("a⁄b"), tr("Fraction (inline)"), QString::fromUtf8("^{[a]}⁄_{b} "), false},
        {QString(), tr("Fraction (stacked, on its own line)"), sStackedFractionMarkup, false},
        {QString::fromUtf8("x²"), tr("Square"), QString::fromUtf8("[x]^{2}"), true},
        {QString::fromUtf8("x³"), tr("Cube"), QString::fromUtf8("[x]^{3}"), true},
        {QString::fromUtf8("xⁿ"), tr("Power (use \"Normal\" to leave the exponent)"), QString::fromUtf8("[x]^{[n]}"), true},
        {QString::fromUtf8("x⁻¹"), tr("Inverse"), QString::fromUtf8("[x]^{−1}"), true},
        {QString::fromUtf8("x₁"), tr("Index"), QString::fromUtf8("[x]_{1}"), true},
        {QString::fromUtf8("xₙ"), tr("Index n (use \"Normal\" to leave the index)"), QString::fromUtf8("[x]_{[n]}"), true},
        {QString::fromUtf8("±"), tr("Plus or minus"), QString::fromUtf8("±"), false},
        {QString::fromUtf8("×"), tr("Times"), QString::fromUtf8("×"), false},
        {QString::fromUtf8("÷"), tr("Divided by"), QString::fromUtf8("÷"), false},
        {QString::fromUtf8("·"), tr("Dot (multiplication)"), QString::fromUtf8("·"), false},
        {QString::fromUtf8("≤"), tr("Less than or equal to"), QString::fromUtf8("≤"), false},
        {QString::fromUtf8("≥"), tr("Greater than or equal to"), QString::fromUtf8("≥"), false},
        {QString::fromUtf8("≠"), tr("Not equal to"), QString::fromUtf8("≠"), false},
        {QString::fromUtf8("≈"), tr("Approximately equal to"), QString::fromUtf8("≈"), false},
        {QString::fromUtf8("≡"), tr("Identical to"), QString::fromUtf8("≡"), false},
        {QString::fromUtf8("→"), tr("Arrow"), QString::fromUtf8("→"), false},
        {QString::fromUtf8("⇒"), tr("Implies"), QString::fromUtf8("⇒"), false},
        {QString::fromUtf8("⇔"), tr("If and only if"), QString::fromUtf8("⇔"), false},
        {QString::fromUtf8("∈"), tr("Element of"), QString::fromUtf8("∈"), false},
        {QString::fromUtf8("∉"), tr("Not an element of"), QString::fromUtf8("∉"), false},
        {QString::fromUtf8("⊂"), tr("Subset of"), QString::fromUtf8("⊂"), false},
        {QString::fromUtf8("∪"), tr("Union"), QString::fromUtf8("∪"), false},
        {QString::fromUtf8("∩"), tr("Intersection"), QString::fromUtf8("∩"), false},
        {QString::fromUtf8("∅"), tr("Empty set"), QString::fromUtf8("∅"), false},
        {QString::fromUtf8("∀"), tr("For all"), QString::fromUtf8("∀"), false},
        {QString::fromUtf8("∃"), tr("There exists"), QString::fromUtf8("∃"), false},
        {QString::fromUtf8("ℕ"), tr("Natural numbers"), QString::fromUtf8("ℕ"), false},
        {QString::fromUtf8("ℤ"), tr("Integers"), QString::fromUtf8("ℤ"), false},
        {QString::fromUtf8("ℚ"), tr("Rational numbers"), QString::fromUtf8("ℚ"), false},
        {QString::fromUtf8("ℝ"), tr("Real numbers"), QString::fromUtf8("ℝ"), false},
        {QString::fromUtf8("°"), tr("Degree"), QString::fromUtf8("°"), false},
        {QString::fromUtf8("%"), tr("Percent"), QString::fromUtf8("%"), false},
        {QString::fromUtf8("‰"), tr("Per mille"), QString::fromUtf8("‰"), false},
        {QString::fromUtf8("∠"), tr("Angle"), QString::fromUtf8("∠"), false},
        {QString::fromUtf8("⊥"), tr("Perpendicular to"), QString::fromUtf8("⊥"), false},
        {QString::fromUtf8("∥"), tr("Parallel to"), QString::fromUtf8("∥"), false}
    }};

    MathPage greek{tr("Greek"), {
        {QString::fromUtf8("α"), tr("alpha"), QString::fromUtf8("α"), false},
        {QString::fromUtf8("β"), tr("beta"), QString::fromUtf8("β"), false},
        {QString::fromUtf8("γ"), tr("gamma"), QString::fromUtf8("γ"), false},
        {QString::fromUtf8("δ"), tr("delta"), QString::fromUtf8("δ"), false},
        {QString::fromUtf8("ε"), tr("epsilon"), QString::fromUtf8("ε"), false},
        {QString::fromUtf8("ζ"), tr("zeta"), QString::fromUtf8("ζ"), false},
        {QString::fromUtf8("η"), tr("eta"), QString::fromUtf8("η"), false},
        {QString::fromUtf8("θ"), tr("theta"), QString::fromUtf8("θ"), false},
        {QString::fromUtf8("λ"), tr("lambda"), QString::fromUtf8("λ"), false},
        {QString::fromUtf8("μ"), tr("mu"), QString::fromUtf8("μ"), false},
        {QString::fromUtf8("π"), tr("pi"), QString::fromUtf8("π"), false},
        {QString::fromUtf8("ρ"), tr("rho"), QString::fromUtf8("ρ"), false},
        {QString::fromUtf8("σ"), tr("sigma"), QString::fromUtf8("σ"), false},
        {QString::fromUtf8("τ"), tr("tau"), QString::fromUtf8("τ"), false},
        {QString::fromUtf8("φ"), tr("phi"), QString::fromUtf8("φ"), false},
        {QString::fromUtf8("χ"), tr("chi"), QString::fromUtf8("χ"), false},
        {QString::fromUtf8("ψ"), tr("psi"), QString::fromUtf8("ψ"), false},
        {QString::fromUtf8("ω"), tr("omega"), QString::fromUtf8("ω"), false},
        {QString::fromUtf8("Δ"), tr("capital Delta"), QString::fromUtf8("Δ"), false},
        {QString::fromUtf8("Σ"), tr("capital Sigma"), QString::fromUtf8("Σ"), false},
        {QString::fromUtf8("Π"), tr("capital Pi"), QString::fromUtf8("Π"), false},
        {QString::fromUtf8("Ω"), tr("capital Omega"), QString::fromUtf8("Ω"), false},
        {QString::fromUtf8("Φ"), tr("capital Phi"), QString::fromUtf8("Φ"), false},
        {QString::fromUtf8("Θ"), tr("capital Theta"), QString::fromUtf8("Θ"), false},
        {QString::fromUtf8("Λ"), tr("capital Lambda"), QString::fromUtf8("Λ"), false}
    }};

    MathPage functions{tr("Functions"), {
        {QString::fromUtf8("sin"), tr("Sine"), QString::fromUtf8("sin(|)"), false},
        {QString::fromUtf8("cos"), tr("Cosine"), QString::fromUtf8("cos(|)"), false},
        {QString::fromUtf8("tan"), tr("Tangent"), QString::fromUtf8("tan(|)"), false},
        {QString::fromUtf8("ln"), tr("Natural logarithm"), QString::fromUtf8("ln(|)"), false},
        {QString::fromUtf8("log"), tr("Logarithm"), QString::fromUtf8("log(|)"), false},
        {QString::fromUtf8("ᵍlog"), tr("Logarithm with base g (written before log)"), QString::fromUtf8("^{[g]}log(x)"), false},
        {QString::fromUtf8("eˣ"), tr("Exponential function (use \"Normal\" to leave the exponent)"), QString::fromUtf8("e^{[x]}"), false},
        {QString::fromUtf8("f(x)"), tr("Function"), QString::fromUtf8("[f](x)"), false},
        {QString::fromUtf8("v⃗"), tr("Vector (arrow)"), QString::fromUtf8("[v]⃗"), false},
        {QString::fromUtf8("AB̅"), tr("Vector or line segment (overline)"), QString::fromUtf8("~{[AB]} "), false}
    }};

    pages << calculus << algebra << greek << functions;

    return pages;
}

QToolButton* UBMathPalette::createTemplateButton(const MathTemplate& tmpl, QWidget* parent)
{
    QToolButton* button = new QToolButton(parent);
    button->setObjectName("MathPaletteButton");
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(false);
    button->setMinimumSize(sButtonWidth, sButtonHeight);
    button->setMaximumHeight(sButtonHeight);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    QFont font = button->font();
    const QString family = mathFontFamily();

    if (!family.isEmpty())
    {
        font.setFamily(family);
    }

    font.setPointSize(13);
    button->setFont(font);

    if (tmpl.markup == sStackedFractionMarkup)
    {
        // no good text label exists for a stacked fraction: draw it
        const qreal dpr = devicePixelRatioF();
        QPixmap pixmap(QSize(28, 28) * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        QFont small = font;
        small.setPixelSize(12);
        painter.setFont(small);
        painter.setPen(QColor("#202020"));
        painter.drawText(QRectF(0, 0, 28, 13), Qt::AlignCenter, "a");
        painter.drawLine(QPointF(7, 14), QPointF(21, 14));
        painter.drawText(QRectF(0, 15, 28, 13), Qt::AlignCenter, "b");
        painter.end();

        button->setIcon(QIcon(pixmap));
        button->setIconSize(QSize(28, 28));
    }
    else
    {
        button->setText(tmpl.label);
    }

    button->setToolTip(tmpl.toolTip);
    button->setProperty("templateIndex", mTemplates.size());
    mTemplates << tmpl;

    connect(button, &QToolButton::clicked, this, &UBMathPalette::templateButtonClicked);

    return button;
}

void UBMathPalette::setPaletteVisible(bool visible)
{
    if (visible != isVisible())
    {
        setVisible(visible);
    }

    if (visible)
    {
        raise();
    }

    UBSettings::settings()->boardShowMathPalette->set(visible);
}

void UBMathPalette::onCloseButtonClicked()
{
    setPaletteVisible(false);
    emit closed();
}

void UBMathPalette::mouseReleaseEvent(QMouseEvent* event)
{
    const bool wasMoving = mIsMoving;

    UBFloatingPalette::mouseReleaseEvent(event);

    if (wasMoving)
    {
        mPreferredPos = pos();
        mHasPreferredPos = true;
        UBSettings::settings()->boardMathPalettePosition->set(mPreferredPos);
    }
}

void UBMathPalette::showEvent(QShowEvent* event)
{
    placeInsideParent();
    UBFloatingPalette::showEvent(event);
}

bool UBMathPalette::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
    {
        placeInsideParent();
    }

    return UBFloatingPalette::eventFilter(watched, event);
}

void UBMathPalette::placeInsideParent()
{
    QWidget* parent = parentWidget();

    if (!parent)
    {
        return;
    }

    resize(sizeHint());

    QPoint target = mPreferredPos;

    if (!mHasPreferredPos)
    {
        // default: centered near the bottom of the board
        target = QPoint((parent->width() - width()) / 2, parent->height() - height() - 100);
    }

    moveInsideParent(target);
}

void UBMathPalette::pageSelected(int index)
{
    if (index >= 0 && index < mPages->count())
    {
        mPages->setCurrentIndex(index);
        UBSettings::settings()->boardMathPaletteTab->set(index);
        updatePageSize();

        if (isVisible())
        {
            placeInsideParent();
        }
    }
}

void UBMathPalette::updatePageSize()
{
    // let the palette fit the current page only: hidden pages are ignored for the height
    for (int i = 0; i < mPages->count(); ++i)
    {
        QWidget* page = mPages->widget(i);
        page->setSizePolicy(QSizePolicy::Preferred, i == mPages->currentIndex() ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    }

    mPages->updateGeometry();

    if (layout())
    {
        layout()->activate();
    }

    resize(sizeHint());
}

UBGraphicsTextItem* UBMathPalette::activeTextItem() const
{
    if (!UBApplication::boardController)
    {
        return nullptr;
    }

    std::shared_ptr<UBGraphicsScene> scene = UBApplication::boardController->activeScene();

    if (!scene)
    {
        return nullptr;
    }

    auto usable = [&scene](UBGraphicsTextItem* item) {
        return item
                && item->scene() == scene
                && item->isVisible()
                && item->data(UBGraphicsItemData::ItemEditable).toBool();
    };

    // the text item being edited has the focus of the scene (the palette never takes the focus)
    UBGraphicsTextItem* item = qgraphicsitem_cast<UBGraphicsTextItem*>(scene->focusItem());

    if (usable(item) && (item->textInteractionFlags() & Qt::TextEditable))
    {
        return item;
    }

    // fall back to the item we inserted into last, as long as it is still selected
    item = mLastTextItem.data();

    if (usable(item) && item->isSelected())
    {
        return item;
    }

    return nullptr;
}

UBGraphicsTextItem* UBMathPalette::createTextItem()
{
    UBBoardController* controller = UBApplication::boardController;
    std::shared_ptr<UBGraphicsScene> scene = controller ? controller->activeScene() : nullptr;
    UBBoardView* view = controller ? controller->controlView() : nullptr;

    if (!scene || !view)
    {
        return nullptr;
    }

    // Same sequence as creating a text zone with the Text tool (see UBBoardView::mouseReleaseEvent):
    // the stylus tool ends up as Selector, so that the new text item stays in edit mode.
    // Switching the tool deselects all items, so do it before creating the item.
    UBDrawingController::drawingController()->setStylusTool(UBStylusTool::Selector);

    const qreal width = scene->nominalSize().width() / controller->currentZoom() / 4.;
    // centre of the visible board, avoiding the part covered by this palette
    QRect freeArea = view->viewport()->rect();

    if (isVisible())
    {
        const QRect paletteRect(view->viewport()->mapFromGlobal(mapToGlobal(QPoint(0, 0))), size());

        if (paletteRect.intersects(freeArea))
        {
            QRect above = freeArea;
            above.setBottom(paletteRect.top());
            QRect below = freeArea;
            below.setTop(paletteRect.bottom());

            const QRect& larger = above.height() >= below.height() ? above : below;

            if (larger.height() > 100)
            {
                freeArea = larger;
            }
        }
    }

    const QPointF center = view->mapToScene(freeArea.center());

    UBGraphicsTextItem* item = scene->addTextHtml("", center - QPointF(width / 2., 0.));

    if (!item)
    {
        return nullptr;
    }

    item->setTextInteractionFlags(Qt::TextEditorInteraction);
    item->setSelected(true);
    item->setTextWidth(width);

    // prefer a font with good maths coverage for this item only (the default font setting is untouched)
    const QString family = mathFontFamily();

    if (!family.isEmpty())
    {
        QFont font = item->font();
        font.setFamily(family);
        item->setFont(font);

        QTextCursor cursor = item->textCursor();
        QTextCharFormat format;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 1, 0))
        format.setFontFamilies(QStringList() << family);
#else
        format.setFontFamily(family);
#endif
        cursor.mergeCharFormat(format);
        item->setTextCursor(cursor);
    }

    item->setFocus();

    return item;
}

void UBMathPalette::focusTextItem(UBGraphicsTextItem* item)
{
    item->setTextInteractionFlags(Qt::TextEditorInteraction);

    if (!item->isSelected())
    {
        item->setSelected(true);
    }

    item->setFocus();

    UBBoardView* view = UBApplication::boardController->controlView();

    if (view && !view->hasFocus())
    {
        view->setFocus();
    }

    mLastTextItem = item;
}

void UBMathPalette::templateButtonClicked()
{
    QObject* button = sender();

    if (!button)
    {
        return;
    }

    bool ok = false;
    const int index = button->property("templateIndex").toInt(&ok);

    if (!ok || index < 0 || index >= mTemplates.size())
    {
        return;
    }

    UBGraphicsTextItem* item = activeTextItem();

    if (!item)
    {
        item = createTextItem();
    }

    if (item)
    {
        insertTemplate(item, mTemplates.at(index));
        focusTextItem(item);
    }
}

void UBMathPalette::normalTextClicked()
{
    UBGraphicsTextItem* item = activeTextItem();

    if (!item)
    {
        return;
    }

    QTextCursor cursor = item->textCursor();
    QTextCharFormat normal;
    normal.setVerticalAlignment(QTextCharFormat::AlignNormal);
    normal.setFontOverline(false);

    if (cursor.hasSelection())
    {
        cursor.mergeCharFormat(normal);
    }
    else
    {
        // only changes the format used for the next typed characters
        QTextCharFormat format = cursor.charFormat();
        format.merge(normal);
        cursor.setCharFormat(format);
    }

    item->setTextCursor(cursor);
    focusTextItem(item);
}

void UBMathPalette::insertTemplate(UBGraphicsTextItem* item, const MathTemplate& tmpl)
{
    QTextCursor cursor = item->textCursor();
    QString wrapText;

    if (cursor.hasSelection())
    {
        const QString selected = cursor.selectedText();

        // only wrap a selection within a single paragraph; otherwise just replace it
        if (!selected.contains(QChar::ParagraphSeparator) && !selected.contains(QChar::ObjectReplacementCharacter))
        {
            wrapText = selected;
        }
    }

    cursor.beginEditBlock();

    if (cursor.hasSelection())
    {
        cursor.removeSelectedText();
    }

    if (tmpl.markup == sStackedFractionMarkup)
    {
        QTextCharFormat format = cursor.charFormat();
        QColor color = item->defaultTextColor();

        if (format.hasProperty(QTextFormat::ForegroundBrush))
        {
            color = format.foreground().color();
        }

        insertStackedFraction(cursor, color, wrapText);
    }
    else
    {
        bool skipBase = false;

        if (tmpl.optionalBase && wrapText.isEmpty() && !cursor.atBlockStart())
        {
            // there is already an operand before the cursor: x² only adds the ²
            const QChar previous = item->document()->characterAt(cursor.position() - 1);
            skipBase = !sNoBaseBefore.contains(previous);
        }

        insertMarkup(cursor, tmpl.markup, skipBase, wrapText);
    }

    cursor.endEditBlock();

    item->setTextCursor(cursor);
}

void UBMathPalette::insertMarkup(QTextCursor& cursor, const QString& markup, bool skipBase, const QString& wrapText) const
{
    // templates are always inserted relative to the normal base line
    QTextCharFormat base = cursor.charFormat();
    base.setVerticalAlignment(QTextCharFormat::AlignNormal);
    base.setFontOverline(false);

    QList<QTextCharFormat> formats;
    formats << base;

    bool wrapPending = !wrapText.isEmpty();
    bool skipPending = skipBase;
    bool skipping = false;
    bool inPlaceholder = false;
    int placeholderStart = -1;

    // where the cursor goes after the insertion: a selected placeholder or a caret
    int targetStart = -1;
    int targetEnd = -1;
    bool targetIsCaret = false;
    QTextCharFormat targetFormat;

    for (int i = 0; i < markup.size(); ++i)
    {
        const QChar ch = markup.at(i);
        const QChar next = i + 1 < markup.size() ? markup.at(i + 1) : QChar();

        if (skipping)
        {
            // skip the content of a placeholder that is omitted or replaced by the selection
            if (ch == '\\')
            {
                ++i;
            }
            else if (ch == ']')
            {
                skipping = false;
            }

            continue;
        }

        if (ch == '\\' && !next.isNull())
        {
            cursor.insertText(QString(next), formats.last());
            ++i;
            continue;
        }

        if ((ch == '^' || ch == '_' || ch == '~') && next == '{')
        {
            QTextCharFormat format = formats.last();

            if (ch == '^')
            {
                format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
            }
            else if (ch == '_')
            {
                format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
            }
            else
            {
                format.setFontOverline(true);
            }

            formats << format;
            ++i;
            continue;
        }

        if (ch == '}' && formats.size() > 1)
        {
            formats.removeLast();
            continue;
        }

        if (ch == '[')
        {
            if (skipPending)
            {
                skipPending = false;
                skipping = true;
            }
            else if (wrapPending)
            {
                wrapPending = false;
                cursor.insertText(wrapText, formats.last());
                skipping = true;
            }
            else
            {
                inPlaceholder = true;
                placeholderStart = cursor.position();
            }

            continue;
        }

        if (ch == ']' && inPlaceholder)
        {
            inPlaceholder = false;

            if (targetStart < 0)
            {
                targetStart = placeholderStart;
                targetEnd = cursor.position();
            }

            continue;
        }

        if (ch == '|')
        {
            if (wrapPending)
            {
                wrapPending = false;
                cursor.insertText(wrapText, formats.last());
            }
            else if (targetStart < 0)
            {
                targetStart = targetEnd = cursor.position();
                targetIsCaret = true;
                targetFormat = formats.last();
            }

            continue;
        }

        cursor.insertText(QString(ch), formats.last());
    }

    if (targetStart >= 0)
    {
        cursor.setPosition(targetStart);

        if (targetIsCaret)
        {
            // the next typed characters get the format of the slot (e.g. superscript)
            cursor.setCharFormat(targetFormat);
        }
        else
        {
            cursor.setPosition(targetEnd, QTextCursor::KeepAnchor);
        }
    }
    else
    {
        // continue in normal text after the template
        cursor.setCharFormat(base);
    }
}

void UBMathPalette::insertStackedFraction(QTextCursor& cursor, const QColor& color, const QString& wrapText) const
{
    QTextCharFormat base = cursor.charFormat();
    base.setVerticalAlignment(QTextCharFormat::AlignNormal);
    base.setFontOverline(false);

    QTextTableFormat tableFormat;
    tableFormat.setBorder(0);
    tableFormat.setBorderStyle(QTextFrameFormat::BorderStyle_None);
    tableFormat.setCellPadding(2);
    tableFormat.setCellSpacing(0);
    tableFormat.setBorderCollapse(true);
    tableFormat.setAlignment(Qt::AlignLeft);

    QTextTable* table = cursor.insertTable(2, 1, tableFormat);

    if (!table)
    {
        return;
    }

    // the fraction bar is the bottom border of the numerator cell
    const QFont font = base.font();
    qreal lineWidth = 1.;

    if (font.pointSizeF() > 0)
    {
        lineWidth = qMax(1., font.pointSizeF() / 12.);
    }
    else if (font.pixelSize() > 0)
    {
        lineWidth = qMax(1., font.pixelSize() / 16.);
    }

    QTextTableCell numerator = table->cellAt(0, 0);
    QTextTableCellFormat numeratorFormat = numerator.format().toTableCellFormat();
    numeratorFormat.setBottomBorder(lineWidth);
    numeratorFormat.setBottomBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    numeratorFormat.setBottomBorderBrush(color);
    numerator.setFormat(numeratorFormat);

    QTextBlockFormat centered;
    centered.setAlignment(Qt::AlignHCenter);

    QTextCursor numeratorCursor = numerator.firstCursorPosition();
    numeratorCursor.setBlockFormat(centered);
    const int numeratorStart = numeratorCursor.position();
    numeratorCursor.insertText(wrapText.isEmpty() ? QStringLiteral("a") : wrapText, base);
    const int numeratorEnd = numeratorCursor.position();

    QTextCursor denominatorCursor = table->cellAt(1, 0).firstCursorPosition();
    denominatorCursor.setBlockFormat(centered);
    const int denominatorStart = denominatorCursor.position();
    denominatorCursor.insertText(QStringLiteral("b"), base);
    const int denominatorEnd = denominatorCursor.position();

    // select the placeholder to be typed over: the numerator, or the denominator if the selection was wrapped
    if (wrapText.isEmpty())
    {
        cursor.setPosition(numeratorStart);
        cursor.setPosition(numeratorEnd, QTextCursor::KeepAnchor);
    }
    else
    {
        cursor.setPosition(denominatorStart);
        cursor.setPosition(denominatorEnd, QTextCursor::KeepAnchor);
    }
}
