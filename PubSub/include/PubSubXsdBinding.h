/* © Copyright CERN, 2026.  All rights not expressly granted are reserved.
 * PubSubXsdBinding.h
 *
 *  Created on: 12 Jul 2026
 *      Author: Paris Moschovakos
 *
 *  This file is part of Quasar.
 *
 *  Quasar is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public Licence as published by
 *  the Free Software Foundation, either version 3 of the Licence.
 *
 *  Quasar is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public Licence for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Quasar.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PUBSUB_INCLUDE_PUBSUBXSDBINDING_H_
#define PUBSUB_INCLUDE_PUBSUBXSDBINDING_H_

#include <PubSubConfiguration.h>

namespace Configuration
{
class PubSub;
}

namespace PubSub
{

Configuration configurationFromXsd(const ::Configuration::PubSub& xsd);

}

#endif /* PUBSUB_INCLUDE_PUBSUBXSDBINDING_H_ */
