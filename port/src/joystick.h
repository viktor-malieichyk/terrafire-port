/* 0x18524: the joystick (here a gamepad) as key presses. In a mission the
 * stick works the configured left / right / thrust keys and the button the
 * fire key; elsewhere the stick is the arrow keys and the button Enter.
 * Added for gamepads: B or Start presses Esc. */
#ifndef TF_JOYSTICK_H
#define TF_JOYSTICK_H

void joystick_poll(void);

#endif
