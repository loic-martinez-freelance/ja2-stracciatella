#include "MapItemReplacementModel.h"
#include "ItemModel.h"

static const ItemModel* GetItemFromValue(const JsonValue& val, const ItemSystem* items)
{
	if (val.isString()) {
		auto str = val.toString();
		return items->getItemByName(str);
	}
	if (val.isInt()) {
		auto id = val.toInt();
		return items->getItem(id);
	}
	throw std::runtime_error("value should be internal name or item id");
}

std::map<uint16_t, uint16_t> MapItemReplacementModel::deserialize(const JsonValue& json, const ItemSystem* items)
{
	std::map<uint16_t, uint16_t> mapping;
	for (auto& item : json.toVec())
	{
		auto m = item.toObject();
		const ItemModel* toItem = GetItemFromValue(m["to"], items);

		// A numeric source id does not have to exist in the current item set:
		// maps built for another item set (e.g. Wildfire) use ids we don't define.
		uint16_t fromIndex;
		auto const& from = m["from"];
		if (from.isInt())
		{
			auto const id = from.toInt();
			if (id < 0 || id > UINT16_MAX) throw std::runtime_error("item id out of range");
			fromIndex = static_cast<uint16_t>(id);
		}
		else
		{
			const ItemModel* fromItem = GetItemFromValue(from, items);
			if (fromItem == NULL) throw std::runtime_error("Item not found");
			fromIndex = fromItem->getItemIndex();
		}

		if (toItem == NULL)
		{
			throw std::runtime_error("Item not found");
		}
		mapping.insert(std::make_pair(fromIndex, toItem->getItemIndex()));
	}

	return mapping;
}
