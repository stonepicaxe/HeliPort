//
//  StatusBarIconManager.swift
//  HeliPort
//
//  Created by 梁怀宇 on 2020/4/7.
//  Copyright © 2020 OpenIntelWireless. All rights reserved.
//

/*
 * This program and the accompanying materials are licensed and made available
 * under the terms and conditions of the The 3-Clause BSD License
 * which accompanies this distribution. The full text of the license may be found at
 * https://opensource.org/licenses/BSD-3-Clause
 */

import Cocoa

protocol StatusBarIconProvider {
    var transition: CATransition? { get }
    var off: NSImage { get }
    var connected: NSImage { get }
    var disconnected: NSImage { get }
    var warning: NSImage { get }
    var scanning: [NSImage] { get }
    func getRssiImage(_ RSSI: Int16) -> NSImage?
}

class StatusBarIcon {
    private static var instance: StatusBarIcon?

    private let statusBar: NSStatusItem
    private let icons: StatusBarIconProvider
    private var timer: Timer?
    private var tickIndex: Int = 0
    private var tickDirection: Int = 1

    private init(_ statusBar: NSStatusItem, _ icons: StatusBarIconProvider) {
        self.statusBar = statusBar
        self.icons = icons
    }

    static func shared(statusBar: NSStatusItem? = nil, icons: StatusBarIconProvider? = nil) -> StatusBarIcon {
        if let instance {
            return instance
        }
        guard let statusBar, let icons else {
            fatalError("Must provide statusBar and iconProvider for the first initialization.")
        }
        instance = StatusBarIcon(statusBar, icons)
        return instance!
    }

    func on() {
        stopTimer()
        disconnected()
    }

    func off() {
        stopTimer()
        DispatchQueue.main.async {
            self.statusBar.button?.image = self.icons.off
        }
    }

    func connected() {
        stopTimer()
        DispatchQueue.main.async {
            self.statusBar.button?.image = self.icons.connected
        }
    }

    func disconnected() {
        stopTimer()
        DispatchQueue.main.async {
            self.statusBar.button?.image = self.icons.disconnected
        }
    }

    func connecting() {
        DispatchQueue.main.async {
            guard self.timer == nil else { return }
            self.tickIndex = 0
            self.tickDirection = 1
            let timer = Timer(
                timeInterval: 0.3,
                target: self,
                selector: #selector(self.tick),
                userInfo: nil,
                repeats: true
            )
            RunLoop.main.add(timer, forMode: .common)
            self.timer = timer
            self.tick()
        }
    }

    func warning() {
        stopTimer()
        DispatchQueue.main.async {
            self.statusBar.button?.image = self.icons.warning
        }
    }

    func error() {
        stopTimer()
        DispatchQueue.main.async {
            self.statusBar.button?.image = #imageLiteral(resourceName: "WiFiStateError")
        }
    }

    func signalStrength(rssi: Int16) {
        stopTimer()
        DispatchQueue.main.async {
            self.statusBar.button?.image = self.icons.getRssiImage(rssi)
        }
    }

    func getRssiImage(rssi: Int16) -> NSImage? {
        return icons.getRssiImage(rssi)
    }

    @objc private func tick() {
        if let transition = self.icons.transition {
            self.statusBar.button?.layer?.add(transition, forKey: kCATransition)
        }
        self.statusBar.button?.image = self.icons.scanning[self.tickIndex]

        self.tickIndex += self.tickDirection
        if self.tickIndex == 0 || self.tickIndex == self.icons.scanning.endIndex - 1 {
            self.tickDirection *= -1
        }
    }

    private func stopTimer() {
        if Thread.isMainThread {
            timer?.invalidate()
            timer = nil
        } else {
            DispatchQueue.main.async {
                self.timer?.invalidate()
                self.timer = nil
            }
        }
    }
}
