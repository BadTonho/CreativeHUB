#pragma once

#include <QString>
#include <QVector>

namespace effects {

struct Category {
    QString id;
    QString name;
};

struct Definition {
    QString id;
    QString name;
    QString category_id;
};

const QVector<Category>& categories();
const QVector<Definition>& definitions();

}  // namespace effects
