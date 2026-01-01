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
    m_label = new QLabel("Click to select video", this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet(
        "color: white;"
        "font-size: 18px;"
        "font-weight: 500;"
        "background: rgba(0,0,0,0.5);" // optional semi-transparent background
        );
    m_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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

// make sure label always fills overlay
void ClickOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_label)
        m_label->setGeometry(rect());
}

// show or hide the label
void ClickOverlay::showText(bool visible)
{
    if (m_label)
        m_label->setVisible(visible);
}

// emit click signal
void ClickOverlay::mousePressEvent(QMouseEvent *event)
{
    qDebug() << "Overlay clicked";
    emit clicked();
}
