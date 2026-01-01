#include "clickoverlay.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QDebug>

ClickOverlay::ClickOverlay(QWidget *parent)
    : QWidget(parent)
{
    // --- Make overlay transparent but still receive mouse events ---
    setAttribute(Qt::WA_NoSystemBackground, true);   // no default background
    setAttribute(Qt::WA_TranslucentBackground, true); // fully transparent
    setAttribute(Qt::WA_StyledBackground, false);    // don't paint background

    // --- Label for clickable text ---
    m_label = new QLabel("Click to select video", this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet(
        "color: white;"
        "font-size: 18px;"
        "font-weight: 500;"
        );
    m_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->addStretch();
    layout->addWidget(m_label, 0, Qt::AlignCenter);  // center text
    layout->addStretch();

    setGeometry(parent->rect()); // match parent size
    show();
    raise();
    update();  // force repaint
}

void ClickOverlay::showText(bool visible)
{
    if (m_label)
        m_label->setVisible(visible);
    update();
}

void ClickOverlay::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    qDebug() << "Overlay clicked";
    emit clicked();
}
