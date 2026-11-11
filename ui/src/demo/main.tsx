// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// Entry point of the demo bundle: the device UI as it is, under a banner that
// says nothing on this page reaches a device.
import { mount } from "../mount";
import "./demo.css";

const banner = document.createElement("div");
banner.className = "demo-banner";
banner.setAttribute("role", "note");
banner.textContent = "Demo: a virtual device, view only. Changes are refused.";
document.body.prepend(banner);

mount();
