#pragma once

#include "levels_editor.h"
#include <creative_suite/effects/curves.h>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <algorithm>
#include <functional>

namespace image_editor {

class CurvesGraph final : public LevelsHistogram {
public:
    using Parameters = creative_suite::effects::CurvesParameters;
    Parameters value;
    int selected = 0;
    std::function<void(int)> select;
    std::function<void(int,int)> add, move;
    std::function<void()> start, finish;
    explicit CurvesGraph(QWidget* parent) : LevelsHistogram(parent) {
        setObjectName("curvesGraph");
        setAccessibleName(QCoreApplication::translate("ImageEditorUI","Curves graph"));
        setMinimumHeight(220);
        setFocusPolicy(Qt::StrongFocus);
    }
protected:
    void paintEvent(QPaintEvent* event) override {
        LevelsHistogram::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().mid().color(),1));
        const QRectF area=plot();
        for(int i=1;i<4;++i) {
            const double x=area.left()+area.width()*i/4;
            const double y=area.top()+area.height()*i/4;
            painter.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
            painter.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
        }
        const auto table=creative_suite::effects::curvesLut(value);
        QPolygonF line;
        for(int x=0;x<256;++x)line<<point(x,table[x]);
        painter.setPen(QPen(palette().highlight().color(),2));
        painter.drawPolyline(line);
        for(int i=0;i<int(value.points.size());++i) {
            painter.setBrush(i==selected?palette().highlight():palette().base());
            painter.setPen(QPen(palette().text().color(),1));
            painter.drawEllipse(point(value.points[i].input,value.points[i].output),5,5);
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if(event->button()!=Qt::LeftButton)return;
        setFocus();
        const int x=inputAt(event->position()), y=outputAt(event->position());
        int nearest=-1;double distance=100;
        for(int i=0;i<int(value.points.size());++i) {
            const auto p=point(value.points[i].input,value.points[i].output);
            const double d=QLineF(p,event->position()).length();
            if(d<distance){distance=d;nearest=i;}
        }
        if(distance<=10) {
            selected=nearest;if(select)select(selected);
        } else if(value.points.size()<16 && x>0 && x<255) {
            int index=1;
            while(index<int(value.points.size()) && value.points[index].input<x)++index;
            if(index<int(value.points.size()) && value.points[index].input!=x &&
                value.points[index-1].input!=x) {
                if(start)start(); if(add)add(x,y);
                selected=index;update();
            }
        }
        dragging_=true;
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if(!dragging_ || selected<0 || selected>=int(value.points.size()))return;
        if(start)start();
        if(move)move(inputAt(event->position()),outputAt(event->position()));
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if(!dragging_)return;
        dragging_=false;if(finish)finish();event->accept();
    }
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Delete && selected>0 && selected+1<int(value.points.size())) {
            if(remove)remove();event->accept();return;
        }
        LevelsHistogram::keyPressEvent(event);
    }
public:
    std::function<void()> remove;
    void stopDrag(){dragging_=false;}
private:
    [[nodiscard]] QRectF plot() const {return QRectF(12,12,std::max(1,width()-25),std::max(1,height()-25));}
    [[nodiscard]] QPointF point(int x,int y) const {
        const auto area=plot();
        return {area.left()+area.width()*x/255.,area.bottom()-area.height()*y/255.};
    }
    [[nodiscard]] int inputAt(const QPointF& p) const {
        return std::clamp(int(std::lround((p.x()-plot().left())*255/plot().width())),0,255);
    }
    [[nodiscard]] int outputAt(const QPointF& p) const {
        return std::clamp(int(std::lround((plot().bottom()-p.y())*255/plot().height())),0,255);
    }
    bool dragging_=false;
};

class CurvesEditor final : public QWidget {
public:
    using Parameters=creative_suite::effects::CurvesParameters;
    using Bins=LevelsHistogram::Bins;
    std::function<void()> started,finished;
    std::function<void(const Parameters&)> changed;
    explicit CurvesEditor(QWidget* parent):QWidget(parent) {
        setObjectName("curvesEditor");
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        graph_=new CurvesGraph(this);layout->addWidget(graph_);
        auto* row=new QHBoxLayout;
        row->addWidget(new QLabel(trUi("Input"),this));
        input_=new QSpinBox(this);input_->setObjectName("curvesInputSpin");
        input_->setRange(0,255);row->addWidget(input_);
        row->addWidget(new QLabel(trUi("Output"),this));
        output_=new QSpinBox(this);output_->setObjectName("curvesOutputSpin");
        output_->setRange(0,255);row->addWidget(output_);
        layout->addLayout(row);
        row=new QHBoxLayout;
        add_=new QPushButton(trUi("Add Point"),this);add_->setObjectName("curvesAddPoint");
        remove_=new QPushButton(trUi("Remove Point"),this);remove_->setObjectName("curvesRemovePoint");
        reset_=new QPushButton(trUi("Reset"),this);reset_->setObjectName("curvesReset");
        row->addWidget(add_);row->addWidget(remove_);row->addWidget(reset_);layout->addLayout(row);
        graph_->select=[this](int index){finish();index_=index;refresh();};
        graph_->add=[this](int x,int y){addPoint(x,y);};
        graph_->move=[this](int x,int y){movePoint(x,y);};
        graph_->remove=[this]{removePoint();};
        graph_->start=[this]{begin();};graph_->finish=[this]{finish();};
        connect(input_,&QSpinBox::valueChanged,this,[this](int x){if(!refreshing_)movePoint(x,value_.points[index_].output);});
        connect(output_,&QSpinBox::valueChanged,this,[this](int y){if(!refreshing_)movePoint(value_.points[index_].input,y);});
        connect(input_,&QSpinBox::editingFinished,this,[this]{finish();});
        connect(output_,&QSpinBox::editingFinished,this,[this]{finish();});
        connect(add_,&QPushButton::clicked,this,[this]{
            int gap=-1,width=1;
            for(int i=1;i<int(value_.points.size());++i) {
                const int current=value_.points[i].input-value_.points[i-1].input;
                if(current>width){gap=i;width=current;}
            }
            if(gap<0)return;
            const int x=(value_.points[gap-1].input+value_.points[gap].input)/2;
            const int y=creative_suite::effects::curvesLut(value_)[x];
            finish();begin();addPoint(x,y);finish();
        });
        connect(remove_,&QPushButton::clicked,this,[this]{removePoint();});
        connect(reset_,&QPushButton::clicked,this,[this]{
            finish();if(value_==Parameters{})return;
            begin();value_={};index_=0;notify();finish();
        });
        refresh();
    }
    void setValue(const QString& id,const Parameters& value) {
        if(id_!=id){finish();id_=id;index_=0;}
        value_=value;index_=std::clamp(index_,0,int(value_.points.size())-1);refresh();
    }
    void setHistogram(const Bins& bins){graph_->setBins(bins);}
    void finish(){graph_->stopDrag();if(editing_){editing_=false;if(finished)finished();}}
private:
    static QString trUi(const char* text){return QCoreApplication::translate("ImageEditorUI",text);}
    void begin(){if(!editing_){editing_=true;if(started)started();}}
    void notify(){refresh();if(changed)changed(value_);}
    void addPoint(int x,int y) {
        if(value_.points.size()>=16)return;
        int index=1;while(index<int(value_.points.size()) && value_.points[index].input<x)++index;
        if(index>=int(value_.points.size()) || x<=value_.points[index-1].input ||
            x>=value_.points[index].input)return;
        begin();value_.points.insert(value_.points.begin()+index,{x,y});index_=index;notify();
    }
    void removePoint() {
        if(index_<=0 || index_+1>=int(value_.points.size()))return;
        finish();begin();value_.points.erase(value_.points.begin()+index_);
        --index_;notify();finish();
    }
    void movePoint(int x,int y) {
        if(index_<0 || index_>=int(value_.points.size()))return;
        const int minimum=index_?value_.points[index_-1].input+1:0;
        const int maximum=index_+1<int(value_.points.size())?value_.points[index_+1].input-1:255;
        x=index_==0?0:index_+1==int(value_.points.size())?255:std::clamp(x,minimum,maximum);
        y=std::clamp(y,0,255);
        if(value_.points[index_]==creative_suite::effects::CurvesPoint{x,y})return;
        begin();value_.points[index_]={x,y};notify();
    }
    void refresh() {
        refreshing_=true;
        graph_->value=value_;graph_->selected=index_;graph_->update();
        const QSignalBlocker input_block(input_),output_block(output_);
        input_->setRange(index_?value_.points[index_-1].input+1:0,
            index_+1<int(value_.points.size())?value_.points[index_+1].input-1:255);
        input_->setValue(value_.points[index_].input);
        input_->setEnabled(index_>0 && index_+1<int(value_.points.size()));
        output_->setValue(value_.points[index_].output);
        add_->setEnabled(value_.points.size()<16);
        remove_->setEnabled(index_>0 && index_+1<int(value_.points.size()));
        reset_->setEnabled(value_!=Parameters{});
        refreshing_=false;
    }
    QString id_;
    Parameters value_;
    int index_=0;
    bool editing_=false,refreshing_=false;
    CurvesGraph* graph_=nullptr;
    QSpinBox *input_=nullptr,*output_=nullptr;
    QPushButton *add_=nullptr,*remove_=nullptr,*reset_=nullptr;
};

} // namespace image_editor
