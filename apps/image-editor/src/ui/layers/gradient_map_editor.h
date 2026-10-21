#pragma once
#include <creative_suite/effects/gradient_map.h>
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QColorDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <functional>
namespace image_editor {
class GradientMapBar final:public QWidget {
public:
    creative_suite::effects::GradientMapParameters value;
    int selected=0; bool dragging=false;
    std::function<void(int)> select;
    std::function<void(double)> move;
    std::function<void()> start,finish;
    explicit GradientMapBar(QWidget* parent):QWidget(parent){setMinimumHeight(43);setFocusPolicy(Qt::StrongFocus);setObjectName("gradientMapBar");setAccessibleName(QCoreApplication::translate("ImageEditorUI","Gradient Map"));}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);QLinearGradient gradient(8,0,width()-8,0);
        for(const auto& s:value.stops)gradient.setColorAt(s.position,QColor(s.color[0],s.color[1],s.color[2]));
        p.fillRect(QRect(8,1,width()-16,24),gradient);
        for(int i=0;i<int(value.stops.size());++i){const int x=8+int(value.stops[i].position*(width()-16));p.setBrush(i==selected?palette().highlight():palette().button());p.setPen(palette().text().color());p.drawPolygon(QPolygon{{x,25},{x-5,37},{x+5,37}});}
    }
    void keyPressEvent(QKeyEvent* e) override {
        if(e->key()==Qt::Key_Left || e->key()==Qt::Key_Right){selected=std::clamp(selected+(e->key()==Qt::Key_Left?-1:1),0,int(value.stops.size())-1);if(select)select(selected);update();e->accept();return;}QWidget::keyPressEvent(e);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if(e->button()!=Qt::LeftButton)return;int index=0;double best=1e9;
        for(int i=0;i<int(value.stops.size());++i){const auto d=std::abs(e->position().x()-8-value.stops[i].position*(width()-16));if(d<best){index=i;best=d;}}
        selected=index;if(select)select(index);dragging=index>0 && index+1<int(value.stops.size());if(dragging && start)start();update();
    }
    void mouseMoveEvent(QMouseEvent* e) override {if(dragging && move)move(std::round(std::clamp((e->position().x()-8)/std::max(1,width()-16),0.,1.)*1000)/1000);}
    void mouseReleaseEvent(QMouseEvent*) override {if(dragging){dragging=false;if(finish)finish();}}
};
class GradientMapEditor final:public QWidget {
public:
    using Parameters=creative_suite::effects::GradientMapParameters;
    std::function<void()> started,finished;
    std::function<void(const Parameters&)> changed;
    explicit GradientMapEditor(QWidget* parent):QWidget(parent) {
        setObjectName("gradientMapEditor");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        layout->addWidget(new QLabel(trUi(QT_TRANSLATE_NOOP("ImageEditorUI","Gradient Map")),this));bar_=new GradientMapBar(this);layout->addWidget(bar_);
        auto* row=new QHBoxLayout;position_=new QDoubleSpinBox(this);position_->setObjectName("gradientMapPosition");position_->setDecimals(1);position_->setSingleStep(.1);position_->setSuffix(" %");position_->setAccessibleName(trUi(QT_TRANSLATE_NOOP("ImageEditorUI","Position")));row->addWidget(position_);
        color_=new QPushButton(trUi(QT_TRANSLATE_NOOP("ImageEditorUI","Color")),this);color_->setObjectName("gradientMapColor");row->addWidget(color_);layout->addLayout(row);
        row=new QHBoxLayout;add_=new QPushButton(trUi(QT_TRANSLATE_NOOP("ImageEditorUI","Add Point")),this);add_->setObjectName("gradientMapAddPoint");remove_=new QPushButton(trUi(QT_TRANSLATE_NOOP("ImageEditorUI","Remove Point")),this);remove_->setObjectName("gradientMapRemovePoint");row->addWidget(add_);row->addWidget(remove_);layout->addLayout(row);
        bar_->select=[this](int index){finish();index_=index;refresh();};bar_->start=[this]{begin();};bar_->finish=[this]{finish();};
        bar_->move=[this](double position){setPosition(position);};
        connect(position_,&QDoubleSpinBox::valueChanged,this,[this](double position){if(!refreshing_)setPosition(position/100.);});
        connect(position_,&QDoubleSpinBox::editingFinished,this,[this]{finish();});
        connect(add_,&QPushButton::clicked,this,[this]{finish();if(value_.stops.size()>=32)return;std::size_t gap=1;for(std::size_t i=2;i<value_.stops.size();++i)if(value_.stops[i].position-value_.stops[i-1].position>value_.stops[gap].position-value_.stops[gap-1].position)gap=i;
            const double t=std::round((value_.stops[gap].position+value_.stops[gap-1].position)*500)/1000;
            const auto color=creative_suite::effects::gradientMapColor(value_,t);begin();value_.stops.insert(value_.stops.begin()+gap,{t,color});index_=int(gap);notify();finish();});
        connect(remove_,&QPushButton::clicked,this,[this]{if(index_<=0 || index_+1>=int(value_.stops.size()))return;finish();begin();value_.stops.erase(value_.stops.begin()+index_);--index_;notify();finish();});
        connect(color_,&QPushButton::clicked,this,[this]{finish();const auto before=value_;const auto& c=value_.stops[index_].color;QColorDialog dialog(QColor(c[0],c[1],c[2]),this);dialog.setObjectName("gradientMapColorDialog");begin();
            connect(&dialog,&QColorDialog::currentColorChanged,this,[this](const QColor& c){value_.stops[index_].color={static_cast<unsigned char>(c.red()),static_cast<unsigned char>(c.green()),static_cast<unsigned char>(c.blue())};notify();});
            if(dialog.exec()!=QDialog::Accepted){value_=before;notify();}finish();});refresh();
    }
    void setValue(const QString& id,const Parameters& value){if(id_!=id){finish();id_=id;index_=0;}value_=value;index_=std::clamp(index_,0,int(value_.stops.size())-1);refresh();}
    void finish(){bar_->dragging=false;if(editing_){editing_=false;if(finished)finished();}}
private:
    static QString trUi(const char* s){return QCoreApplication::translate("ImageEditorUI",s);}
    void begin(){if(!editing_){editing_=true;if(started)started();}}
    void setPosition(double t){if(index_<=0 || index_+1>=int(value_.stops.size()))return;t=std::clamp(t,value_.stops[index_-1].position+.001,value_.stops[index_+1].position-.001);if(std::abs(value_.stops[index_].position-t)<1e-9)return;begin();value_.stops[index_].position=t;notify();}
    void notify(){const auto value=value_;refresh();if(changed)changed(value);}
    void refresh(){refreshing_=true;bar_->value=value_;bar_->selected=index_;bar_->update();const QSignalBlocker block(position_);const bool internal=index_>0 && index_+1<int(value_.stops.size());position_->setRange(internal?(value_.stops[index_-1].position+.001)*100:0,internal?(value_.stops[index_+1].position-.001)*100:100);position_->setValue(value_.stops[index_].position*100);position_->setEnabled(internal);remove_->setEnabled(internal);add_->setEnabled(value_.stops.size()<32);const auto& c=value_.stops[index_].color;color_->setStyleSheet(QString("background-color: rgb(%1,%2,%3)").arg(c[0]).arg(c[1]).arg(c[2]));refreshing_=false;}
    QString id_;Parameters value_;int index_=0;bool editing_=false,refreshing_=false;
    GradientMapBar* bar_;QDoubleSpinBox* position_;QPushButton *color_,*add_,*remove_;
};
} // namespace image_editor
