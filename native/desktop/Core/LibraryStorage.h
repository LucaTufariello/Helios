#pragma once
#include "Products.h"
#include <functional>
namespace godeye {
using TrashMove = std::function<bool(const QString &, QString *)>;
QString trashOwnedProduct(const QString &jobsRoot, const ProductLayer &product,
                          TrashMove move = {});
} // namespace godeye
