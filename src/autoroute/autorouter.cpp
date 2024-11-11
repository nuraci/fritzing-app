/*******************************************************************

Part of the Fritzing project - http://fritzing.org
Copyright (c) 2007-2019 Fritzing

Fritzing is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.a

Fritzing is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fritzing.  If not, see <http://www.gnu.org/licenses/>.

********************************************************************/

#include "autorouter.h"
#include "../sketch/pcbsketchwidget.h"
#include "../debugdialog.h"
#include "../items/symbolpaletteitem.h"
#include "../items/virtualwire.h"
#include "../items/tracewire.h"
#include "../items/jumperitem.h"
#include "../items/via.h"
#include "../connectors/connectoritem.h"
#include "../items/moduleidnames.h"
#include "../processeventblocker.h"
#include "../referencemodel/referencemodel.h"

#include <qmath.h>
#include <QApplication>
#include <QSettings>

const QString Autorouter::MaxCyclesName("cmrouter/maxcycles");

Autorouter::Autorouter(PCBSketchWidget * sketchWidget) : m_sketchWidget(sketchWidget)
{
}

void Autorouter::cleanUpNets() {
	Q_FOREACH (QList<ConnectorItem *> * connectorItems, m_allPartConnectorItems) {
		delete connectorItems;
	}
	m_allPartConnectorItems.clear();
}

void Autorouter::updateRoutingStatus() {
	RoutingStatus routingStatus;
	routingStatus.zero();
	m_sketchWidget->updateRoutingStatus(routingStatus, true);
}

TraceWire * Autorouter::drawOneTrace(QPointF fromPos, QPointF toPos, double width, ViewLayer::ViewLayerPlacement viewLayerPlacement)
{
	//DebugDialog::debug(QString("trace %1,%2 %3,%4").arg(fromPos.x()).arg(fromPos.y()).arg(toPos.x()).arg(toPos.y()));
#ifndef QT_NO_DEBUG
	if (qAbs(fromPos.x() - toPos.x()) < 0.01 && qAbs(fromPos.y() - toPos.y()) < 0.01) {
		DebugDialog::debug("zero length trace", fromPos);
	}
#endif

	auto newID = ItemBase::getNextID();
	ViewGeometry viewGeometry;
	viewGeometry.setWireFlags(m_sketchWidget->getTraceFlag());
	viewGeometry.setAutoroutable(true);
	viewGeometry.setLoc(fromPos);
	QLineF line(0, 0, toPos.x() - fromPos.x(), toPos.y() - fromPos.y());
	viewGeometry.setLine(line);

	auto *trace = m_sketchWidget->addItem(m_sketchWidget->referenceModel()->retrieveModelPart(ModuleIDNames::WireModuleIDName),
	                   viewLayerPlacement, BaseCommand::SingleView, viewGeometry, newID, -1, nullptr);
	if (trace == nullptr) {
		// we're in trouble
		DebugDialog::debug("autorouter unable to draw one trace");
		return nullptr;
	}

	// addItem calls trace->setSelected(true) so unselect it (TODO: this may no longer be necessary)
	trace->setSelected(false);
	auto *traceWire = dynamic_cast<TraceWire *>(trace);
	if (traceWire == nullptr) {
		DebugDialog::debug("autorouter unable to draw one trace as trace");
		return nullptr;
	}


	m_sketchWidget->setClipEnds(traceWire, false);
	traceWire->setColorString(m_sketchWidget->traceColor(viewLayerPlacement), 1.0, false);
	traceWire->setWireWidth(width, m_sketchWidget, m_sketchWidget->getWireStrokeWidth(traceWire, width));

	return traceWire;
}

void Autorouter::cancel() {
	m_cancelled = true;
}

void Autorouter::cancelTrace() {
	m_cancelTrace = true;
}

void Autorouter::stopTracing() {
	m_stopTracing = true;
}

void Autorouter::useBest() {
	m_useBest = m_stopTracing = true;
}

void Autorouter::initUndo(QUndoCommand * parentCommand)
{
	// autoroutable traces, jumpers and vias are saved on the undo command and deleted
	// non-autoroutable traces, jumpers and via are not deleted

	QList<ItemBase *> toDelete;
	QList<QGraphicsItem *> collidingItems;
	if (m_pcbType) {
		collidingItems = m_sketchWidget->scene()->collidingItems(m_board);
		Q_FOREACH (QGraphicsItem * item, collidingItems) {
			auto *jumperItem = dynamic_cast<JumperItem *>(item);
			if (jumperItem == nullptr) continue;

			if (jumperItem->getAutoroutable()) {
				addUndoConnection(false, jumperItem, parentCommand);
				toDelete.append(jumperItem);
				continue;
			}

			// deal with the traces connecting the jumperitem to the part
			QList<ConnectorItem *> both;
			Q_FOREACH (ConnectorItem * ci, jumperItem->connector0()->connectedToItems()) both.append(ci);
			Q_FOREACH (ConnectorItem * ci, jumperItem->connector1()->connectedToItems()) both.append(ci);
			Q_FOREACH (ConnectorItem * connectorItem, both) {
				auto *w = qobject_cast<TraceWire *>(connectorItem->attachedTo());
				if (w == nullptr) continue;
				if (!w->isTraceType(m_sketchWidget->getTraceFlag())) continue;

				QList<Wire *> wires;
				QList<ConnectorItem *> ends;
				w->collectChained(wires, ends);
				Q_FOREACH (Wire * wire, wires) {
					// make sure the jumper item doesn't lose its wires
					wire->setAutoroutable(false);
				}
			}
		}
		Q_FOREACH (QGraphicsItem * item, collidingItems) {
			auto *via = dynamic_cast<Via *>(item);
			if (via == nullptr) continue;

			if (via->getAutoroutable()) {
				addUndoConnection(false, via, parentCommand);
				toDelete.append(via);
				continue;
			}

			// deal with the traces connecting the via to the part
			QList<ConnectorItem *> both;
			Q_FOREACH (ConnectorItem * ci, via->connectorItem()->connectedToItems()) both.append(ci);
			Q_FOREACH (ConnectorItem * ci, via->connectorItem()->getCrossLayerConnectorItem()->connectedToItems()) both.append(ci);
			Q_FOREACH (ConnectorItem * connectorItem, both) {
				auto *w = qobject_cast<TraceWire *>(connectorItem->attachedTo());
				if (w == nullptr) continue;
				if (!w->isTraceType(m_sketchWidget->getTraceFlag())) continue;

				QList<Wire *> wires;
				QList<ConnectorItem *> ends;
				w->collectChained(wires, ends);
				Q_FOREACH (Wire * wire, wires) {
					// make sure the via doesn't lose its wires
					wire->setAutoroutable(false);
				}
			}
		}
	}
	else {
		collidingItems = m_sketchWidget->scene()->items();
		Q_FOREACH (QGraphicsItem * item, collidingItems) {
			auto *netLabel = dynamic_cast<SymbolPaletteItem *>(item);
			if (netLabel == nullptr) continue;
			if (!netLabel->isOnlyNetLabel()) continue;

			if (netLabel->getAutoroutable()) {
				addUndoConnection(false, netLabel, parentCommand);
				toDelete.append(netLabel);
				continue;
			}

			// deal with the traces connecting the netlabel to the part
			Q_FOREACH (ConnectorItem * connectorItem, netLabel->connector0()->connectedToItems()) {
				auto *w = qobject_cast<TraceWire *>(connectorItem->attachedTo());
				if (w == nullptr) continue;
				if (!w->isTraceType(m_sketchWidget->getTraceFlag())) continue;

				QList<Wire *> wires;
				QList<ConnectorItem *> ends;
				w->collectChained(wires, ends);
				Q_FOREACH (Wire * wire, wires) {
					// make sure the netlabel doesn't lose its wires
					wire->setAutoroutable(false);
				}
			}
		}
	}

	QList<TraceWire *> visited;
	Q_FOREACH (QGraphicsItem * item, collidingItems) {
		auto *traceWire = dynamic_cast<TraceWire *>(item);
		if (traceWire == nullptr) continue;
		if (!traceWire->isTraceType(m_sketchWidget->getTraceFlag())) continue;
		if (traceWire->getAutoroutable()) continue;
		if (visited.contains(traceWire)) continue;

		QList<Wire *> wires;
		QList<ConnectorItem *> ends;
		traceWire->collectChained(wires, ends);
		Q_FOREACH (Wire * wire, wires) {
			visited << qobject_cast<TraceWire *>(wire);
			if (wire->getAutoroutable()) {
				wire->setAutoroutable(false);
			}
		}
	}

	Q_FOREACH (QGraphicsItem * item, collidingItems) {
		auto *traceWire = dynamic_cast<TraceWire *>(item);
		if (traceWire == nullptr) continue;
		if (!traceWire->isTraceType(m_sketchWidget->getTraceFlag())) continue;
		if (!traceWire->getAutoroutable()) continue;

		toDelete.append(traceWire);
		addUndoConnection(false, traceWire, parentCommand);
	}

	Q_FOREACH (ItemBase * itemBase, toDelete) {
		m_sketchWidget->makeDeleteItemCommand(itemBase, BaseCommand::CrossView, parentCommand);
	}

	Q_FOREACH (ItemBase * itemBase, toDelete) {
		m_sketchWidget->deleteItem(itemBase, true, true, false);
	}
}

void Autorouter::addUndoConnection(bool connect, Via * via, QUndoCommand * parentCommand) {
	addUndoConnection(connect, via->connectorItem(), BaseCommand::CrossView, parentCommand);
	addUndoConnection(connect, via->connectorItem()->getCrossLayerConnectorItem(), BaseCommand::CrossView, parentCommand);
}

void Autorouter::addUndoConnection(bool connect, SymbolPaletteItem * netLabel, QUndoCommand * parentCommand) {
	addUndoConnection(connect, netLabel->connector0(), BaseCommand::CrossView, parentCommand);
}

void Autorouter::addUndoConnection(bool connect, JumperItem * jumperItem, QUndoCommand * parentCommand) {
	addUndoConnection(connect, jumperItem->connector0(), BaseCommand::CrossView, parentCommand);
	addUndoConnection(connect, jumperItem->connector1(), BaseCommand::CrossView, parentCommand);
}

void Autorouter::addUndoConnection(bool connect, TraceWire * traceWire, QUndoCommand * parentCommand) {
	addUndoConnection(connect, traceWire->connector0(), BaseCommand::CrossView, parentCommand);
	addUndoConnection(connect, traceWire->connector1(), BaseCommand::CrossView, parentCommand);
}

void Autorouter::addUndoConnection(bool connect, ConnectorItem * connectorItem, BaseCommand::CrossViewType crossView, QUndoCommand * parentCommand)
{
	Q_FOREACH (ConnectorItem * toConnectorItem, connectorItem->connectedToItems()) {
		auto *vw = qobject_cast<VirtualWire *>(toConnectorItem->attachedTo());
		if (vw != nullptr) continue;

		auto *ccc = new ChangeConnectionCommand(m_sketchWidget, crossView,
				toConnectorItem->attachedToID(), toConnectorItem->connectorSharedID(),
				connectorItem->attachedToID(), connectorItem->connectorSharedID(),
				ViewLayer::specFromID(toConnectorItem->attachedToViewLayerID()),
				connect, parentCommand);
		ccc->setUpdateConnections(false);
	}
}

void Autorouter::restoreOriginalState(QUndoCommand * parentCommand) {
	QUndoStack undoStack;
	undoStack.push(parentCommand);
	undoStack.undo();
}

void Autorouter::clearTracesAndJumpers() {
	QList<ItemBase *> toDelete;

	Q_FOREACH (QGraphicsItem * item, (m_board == nullptr) ? m_sketchWidget->scene()->items() : m_sketchWidget->scene()->collidingItems(m_board)) {
		if (m_pcbType) {
			auto *jumperItem = dynamic_cast<JumperItem *>(item);
			if (jumperItem != nullptr) {
				if (jumperItem->getAutoroutable()) {
					toDelete.append(jumperItem);
				}
				continue;
			}
			auto *via = dynamic_cast<Via *>(item);
			if (via != nullptr) {
				if (via->getAutoroutable()) {
					toDelete.append(via);
				}
				continue;
			}
		}
		else {
			auto *netLabel = dynamic_cast<SymbolPaletteItem *>(item);
			if ((netLabel != nullptr) && netLabel->isOnlyNetLabel()) {
				if (netLabel->getAutoroutable()) {
					toDelete.append(netLabel);
				}
				continue;
			}
		}

		auto *traceWire = dynamic_cast<TraceWire *>(item);
		if (traceWire != nullptr) {
			if (traceWire->isTraceType(m_sketchWidget->getTraceFlag()) && traceWire->getAutoroutable()) {
				toDelete.append(traceWire);
			}
			continue;
		}
	}

	Q_FOREACH (ItemBase * itemBase, toDelete) {
		m_sketchWidget->deleteItem(itemBase, true, true, false);
	}
}

void Autorouter::doCancel(QUndoCommand * parentCommand) {
	Q_EMIT setProgressMessage(tr("Routing canceled! Now cleaning up..."));
	ProcessEventBlocker::processEvents();
	restoreOriginalState(parentCommand);
	cleanUpNets();
}

void Autorouter::addToUndo(QUndoCommand * parentCommand)
{
	QList<TraceWire *> wires;
	QList<JumperItem *> jumperItems;
	QList<Via *> vias;
	QList<SymbolPaletteItem *> netLabels;
	Q_FOREACH (QGraphicsItem * item, (m_board  == nullptr) ? m_sketchWidget->scene()->items() : m_sketchWidget->scene()->collidingItems(m_board)) {
		auto *wire = dynamic_cast<TraceWire *>(item);
		if (wire != nullptr) {
			if (!wire->getAutoroutable()) continue;
			if (!wire->isTraceType(m_sketchWidget->getTraceFlag())) continue;

			m_sketchWidget->setClipEnds(wire, true);
			wire->update();
			addWireToUndo(wire, parentCommand);
			wires.append(wire);
			continue;
		}
		if (m_pcbType) {
			auto *jumperItem = dynamic_cast<JumperItem *>(item);
			if (jumperItem != nullptr) {
				jumperItems.append(jumperItem);
				if (!jumperItem->getAutoroutable()) {
					continue;
				}

				jumperItem->saveParams();
				QPointF pos, c0, c1;
				jumperItem->getParams(pos, c0, c1);
                /// @todo this can cause leaks if the constructors of these
                /// commands change.
				new AddItemCommand(m_sketchWidget, BaseCommand::CrossView, ModuleIDNames::JumperModuleIDName, jumperItem->viewLayerPlacement(), jumperItem->getViewGeometry(), jumperItem->id(), false, -1, parentCommand);
				new ResizeJumperItemCommand(m_sketchWidget, jumperItem->id(), pos, c0, c1, pos, c0, c1, parentCommand);
				new CheckStickyCommand(m_sketchWidget, BaseCommand::SingleView, jumperItem->id(), false, CheckStickyCommand::RemoveOnly, parentCommand);

				continue;
			}
			auto *via = dynamic_cast<Via *>(item);
			if (via != nullptr) {
				vias.append(via);
				if (!via->getAutoroutable()) {
					continue;
				}

				new AddItemCommand(m_sketchWidget, BaseCommand::CrossView, ModuleIDNames::ViaModuleIDName, via->viewLayerPlacement(), via->getViewGeometry(), via->id(), false, -1, parentCommand);
				new CheckStickyCommand(m_sketchWidget, BaseCommand::SingleView, via->id(), false, CheckStickyCommand::RemoveOnly, parentCommand);
				new SetPropCommand(m_sketchWidget, via->id(), "hole size", via->holeSize(), via->holeSize(), true, parentCommand);

				continue;
			}
		}
		else {
			auto *netLabel = dynamic_cast<SymbolPaletteItem *>(item);
			if ((netLabel != nullptr) && netLabel->isOnlyNetLabel()) {
				netLabels.append(netLabel);
				if (!netLabel->getAutoroutable()) {
					continue;
				}

				new AddItemCommand(m_sketchWidget, BaseCommand::CrossView, netLabel->moduleID(), netLabel->viewLayerPlacement(), netLabel->getViewGeometry(), netLabel->id(), false, -1, parentCommand);
				new SetPropCommand(m_sketchWidget, netLabel->id(), "label", netLabel->label(), netLabel->label(), true, parentCommand);

				continue;
			}

		}
	}

	Q_FOREACH (TraceWire * traceWire, wires) {
		//traceWire->debugInfo("trace");
		addUndoConnection(true, traceWire, parentCommand);
	}
	Q_FOREACH (JumperItem * jumperItem, jumperItems) {
		addUndoConnection(true, jumperItem, parentCommand);
	}
	Q_FOREACH (Via * via, vias) {
		addUndoConnection(true, via, parentCommand);
	}
	Q_FOREACH (SymbolPaletteItem * netLabel, netLabels) {
		addUndoConnection(true, netLabel, parentCommand);
	}
}

void Autorouter::addWireToUndo(Wire * wire, QUndoCommand * parentCommand)
{
	if (wire == nullptr) return;
    /// @todo this can cause leaks if the ctor of the commands change
	new AddItemCommand(m_sketchWidget, BaseCommand::CrossView, ModuleIDNames::WireModuleIDName, wire->viewLayerPlacement(), wire->getViewGeometry(), wire->id(), false, -1, parentCommand);
	new CheckStickyCommand(m_sketchWidget, BaseCommand::SingleView, wire->id(), false, CheckStickyCommand::RemoveOnly, parentCommand);

	new WireWidthChangeCommand(m_sketchWidget, wire->id(), wire->wireWidth(), wire->wireWidth(), parentCommand);
	new WireColorChangeCommand(m_sketchWidget, wire->id(), wire->colorString(), wire->colorString(), wire->opacity(), wire->opacity(), parentCommand);
}

void Autorouter::setMaxCycles(int maxCycles)
{
	m_maxCycles = maxCycles;
	QSettings settings;
	settings.setValue(MaxCyclesName, maxCycles);
}
