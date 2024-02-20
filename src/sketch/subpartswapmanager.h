/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2024 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Fritzing is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fritzing.  If not, see <http://www.gnu.org/licenses/>.

********************************************************************/

#ifndef SUBPART_SWAP_MANAGER_H
#define SUBPART_SWAP_MANAGER_H

#include <QMap>
#include <QPointer>
#include <QString>

class ReferenceModel;
class ItemBase;
class ViewGeometry;
class ModelPartShared;

typedef QString NewMainModuleID;
typedef QString NewSubModuleID;
typedef QString OldSubModuleID;
typedef long NewModelIndex;
typedef long NewSubID;

class SubpartSwapManager {
public:
	explicit SubpartSwapManager(ReferenceModel *referenceModel);

	//-------------------------------------------------------------------------------------------
	// View independent function to be used once per swap session
	void generateSubpartModelIndices(const NewMainModuleID &newModuleID);
	void correlateOldAndNewSubparts(const NewMainModuleID &newModuleID, ItemBase *itemBase);
	//-------------------------------------------------------------------------------------------

	const QList< QPointer<ModelPartShared> > & subparts(const NewMainModuleID &newModuleID);
	void resetOldSubParts(ItemBase * itemBase);
	ItemBase * extractSubPart(const NewSubModuleID & newModuleID);
	bool newModuleIDWasCorrelated(const NewSubModuleID & newModuleID) const;
	NewModelIndex getNewModelIndex(const NewSubModuleID &newModuleID) const;
	QList<NewSubModuleID> getNewModuleIDs() const;

	NewSubID getNewSubID(const NewSubModuleID &newModuleID) const;

private:
	OldSubModuleID getOldModuleID(const NewSubModuleID &newModuleID) const;

private:
	QPointer<class ReferenceModel> m_referenceModel;

	//-------------------------------------------------------------------------------------------
	// View independent members
	QMap<NewSubModuleID, NewModelIndex> m_subPartNewModuleID2NewModelIndexMap;
	QMap<NewSubModuleID, NewSubID> m_subPartNewModuleID2NewSubIDMap;
	QMap<NewSubModuleID, OldSubModuleID> m_subPartModuleIDNew2OldMap;
	//-------------------------------------------------------------------------------------------

	QMap<OldSubModuleID, ItemBase *> m_subPartMap;
};

#endif // SUBPART_SWAP_MANAGER_H
