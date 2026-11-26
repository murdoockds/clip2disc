#include "clickoverlay.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QDebug>

ClickOverlay::ClickOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_StyledBackground, false);
    setStyleSheet("background: transparent;"); // fully transparent

    // --- Label ---
    m_label = new QLabel("Drop a video here or click to select\nMP4 • MKV • WebM • AVI • MOV", this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet(
        "color: #DBDEE1;"
        "font-size: 16px;"
        "font-weight: 600;"
        "padding: 18px 28px;"
        "border: 2px dashed #5865F2;"
        "border-radius: 12px;"
        "background: rgba(30, 31, 34, 0.78);"
        );
    m_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_label->setVisible(true);

    // Inside ClickOverlay constructor
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    // Ensure it doesn't block the video widget from updating
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    // --- Layout ---
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();
    layout->addWidget(m_label, 0, Qt::AlignCenter);
    layout->addStretch();

    raise();    // make sure it's on top
    show();
}

// keep overlay centered cleanly
void ClickOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

// show or hide the label
void ClickOverlay::showText(bool visible)
{
    if (m_label)
        m_label->setVisible(visible);
}

void ClickOverlay::setText(const QString &text)
{
    if (m_label)
        m_label->setText(text);
}

void ClickOverlay::setClickable(bool clickable)
{
    m_clickable = clickable;
}

// emit click signal
void ClickOverlay::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (!m_clickable)
        return;
    qDebug() << "Overlay clicked";
    emit clicked();
}
