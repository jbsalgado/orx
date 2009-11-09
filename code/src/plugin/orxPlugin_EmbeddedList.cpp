/* Orx - Portable Game Engine
 *
 * Orx is the legal property of its developers, whose names
 * are listed in the COPYRIGHT file distributed 
 * with this source distribution.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file orxPlugin_EmbeddedList.cpp
 * @date 03/05/2009
 * @author iarwain@orx-project.org
 *
 */

#ifdef __orxEMBEDDED__

#ifdef __orxWII__

//! TODO: Includes all plugins to embed for Wii

#else /* __orxWII__ */

/** Includes all plugins to embed
 */
#include <../plugins/Display/SFML/orxDisplay.cpp>
#include <../plugins/Joystick/SFML/orxJoystick.cpp>
#include <../plugins/Keyboard/SFML/orxKeyboard.cpp>
#include <../plugins/Mouse/SFML/orxMouse.cpp>
#include <../plugins/Physics/Box2D/orxPhysics.cpp>
#include <../plugins/Render/Home/orxRender.c>
#include <../plugins/Sound/SFML/orxSoundSystem.cpp>
#include <../plugins/System/SFML/orxSystem.cpp>

#endif /* __orxWII__ */

#endif /* __orxEMBEDDED__ */
