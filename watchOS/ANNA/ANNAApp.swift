import SwiftUI

@main struct ANNAApp: App {
    var body: some Scene {
        WindowGroup { WatchHome() }
    }
}

struct WatchHome: View {
    @StateObject private var conversation = Conversation()
    @Environment(\.scenePhase) private var scenePhase
    @State private var touching = false
    @State private var showReply = false

    var body: some View {
        GeometryReader { geometry in
            let width = geometry.size.width
            let height = geometry.size.height
            ZStack(alignment: .topLeading) {
                RadialGradient(colors: [Color(red: 0.13, green: 0.15, blue: 0.31),
                                        Color(red: 0.05, green: 0.08, blue: 0.18),
                                        Color(red: 0.02, green: 0.03, blue: 0.08)],
                               center: .center, startRadius: 8, endRadius: height * 0.58)

                HStack(spacing: 4) {
                    Image(systemName: "display").font(.system(size: 12, weight: .medium))
                    Circle().fill(conversation.connected ? Color.green : Color.gray)
                        .frame(width: 4, height: 4)
                }
                .foregroundStyle(Color(red: 0.67, green: 0.73, blue: 0.83))
                .accessibilityLabel(conversation.connected ? String(localized: "Mac connected") : String(localized: "Mac disconnected"))
                .padding(.leading, width * 0.12)
                .padding(.top, height * 0.075)

                AnnaCharacter(phase: conversation.phase)
                    .frame(width: width * 0.92, height: width * 0.92)
                    .contentShape(.ellipse)
                    .position(x: width * 0.5, y: height * 0.52)
                    .gesture(DragGesture(minimumDistance: 0)
                        .onChanged { _ in
                            guard !touching, !conversation.calling else { return }
                            touching = true
                            conversation.press()
                        }
                        .onEnded { _ in
                            touching = false
                            conversation.release()
                        })
                    .accessibilityLabel("Dot")
                    .accessibilityHint(String(localized: "Hold to speak, release to send"))

                if !conversation.status.isEmpty {
                    Text(conversation.status)
                        .font(.system(size: 11, weight: .medium))
                        .foregroundStyle(Color(red: 0.73, green: 0.84, blue: 1))
                        .multilineTextAlignment(.center)
                        .frame(width: width * 0.85)
                        .position(x: width * 0.5, y: height * 0.19)
                        .allowsHitTesting(false)
                }

                if conversation.calling {
                    glassButton(String(localized: "Hang Up"), icon: "phone.down.fill", tint: .red, action: conversation.hangup)
                        .position(x: width * 0.5, y: height * 0.57)
                } else if conversation.phase == .idle {
                    glassButton(String(localized: "Call"), icon: "phone.fill", tint: Color(red: 0.62, green: 0.72, blue: 0.89), action: conversation.dial)
                        .position(x: width * 0.5, y: height * 0.86)
                }

                if !conversation.reply.isEmpty, !conversation.calling, conversation.phase == .idle {
                    Button(String(localized: "Read Reply")) { showReply = true }
                        .buttonStyle(.plain)
                        .font(.system(size: 11))
                        .foregroundStyle(Color(red: 0.73, green: 0.84, blue: 1))
                        .position(x: width * 0.5, y: height * 0.20)
                }
            }
        }
        .ignoresSafeArea()
        .task(id: scenePhase) {
            if scenePhase == .active { await conversation.monitor() }
            else if scenePhase == .background {
                touching = false
                conversation.pause()
            }
        }
        .sheet(isPresented: $showReply) {
            ScrollView { Text(conversation.reply).frame(maxWidth: .infinity, alignment: .leading) }
        }
        .onChange(of: conversation.notice?.id) { _, id in
            if id != nil { showReply = false }
        }
        .overlay {
            if let notice = conversation.notice {
                GeometryReader { geometry in
                    VStack(alignment: .leading, spacing: 8) {
                        Label(String(localized: "New Dot Message"), systemImage: "bell.fill")
                            .font(.footnote.weight(.semibold))
                            .foregroundStyle(Color(red: 1, green: 0.77, blue: 0.55))
                        ScrollView {
                            Text(notice.text)
                                .font(.footnote)
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }
                        Button(action: conversation.dismissNotice) {
                            Text(String(localized: "Close"))
                                .font(.footnote.weight(.semibold))
                                .frame(maxWidth: .infinity, minHeight: 36)
                                .background(.white.opacity(0.12), in: Capsule())
                        }
                        .buttonStyle(.plain)
                    }
                    .padding(12)
                    .frame(height: geometry.size.height * 0.94)
                    .background(Color(red: 0.10, green: 0.12, blue: 0.24), in: RoundedRectangle(cornerRadius: 22))
                    .overlay(RoundedRectangle(cornerRadius: 22).strokeBorder(.white.opacity(0.15)))
                    .padding(.horizontal, 10)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
                .background { Color.black.opacity(0.60).ignoresSafeArea() }
            }
        }
    }

    private func glassButton(_ title: String, icon: String, tint: Color,
                             action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Label(title, systemImage: icon)
                .font(.system(size: 12, weight: .semibold))
                .padding(.horizontal, 13)
                .padding(.vertical, 11)
                .background(tint.opacity(0.18), in: Capsule())
                .overlay(Capsule().strokeBorder(tint.opacity(0.48), lineWidth: 1))
                .frame(minHeight: 44)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

private struct AnnaCharacter: View {
    let phase: Conversation.Phase
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        TimelineView(.animation(minimumInterval: 1 / 24, paused: reduceMotion)) { timeline in
            let time = reduceMotion ? 0 : timeline.date.timeIntervalSinceReferenceDate
            let wave = sin(time * (phase == .speaking ? 10 : 1.9))
            let blink = phase == .idle && time.truncatingRemainder(dividingBy: 5) > 4.84
            ZStack {
                Image("AnnaBody").resizable().scaledToFit()
                Canvas { context, size in
                    let unit = size.width / 100
                    func oval(_ x: CGFloat, _ y: CGFloat, _ rx: CGFloat, _ ry: CGFloat) -> Path {
                        Path(ellipseIn: CGRect(x: (x - rx) * unit, y: (y - ry) * unit,
                                               width: 2 * rx * unit, height: 2 * ry * unit))
                    }
                    let ink = Color(red: 0.15, green: 0.09, blue: 0.08)
                    for x in [38.5, 57.0] {
                        context.fill(oval(x, 39.5, 5.5, blink && x == 38.5 ? 0.8 : 6.2), with: .color(ink))
                        if !blink || x != 38.5 {
                            context.fill(oval(x - 1.9, 37.3, 1.7, 1.7),
                                         with: .color(Color(red: 1, green: 0.97, blue: 0.87)))
                        }
                    }
                    context.stroke(oval(57, 39.5, 8.6, 8.6), with: .color(ink), lineWidth: 2.5 * unit)
                    var chain = Path()
                    chain.move(to: CGPoint(x: 64.7 * unit, y: 44 * unit))
                    chain.addCurve(to: CGPoint(x: 76.5 * unit, y: 56.7 * unit),
                                   control1: CGPoint(x: 65.7 * unit, y: 52 * unit),
                                   control2: CGPoint(x: 69.4 * unit, y: 56.3 * unit))
                    context.stroke(chain, with: .color(ink),
                                   style: StrokeStyle(lineWidth: 2.5 * unit, lineCap: .round))
                    if phase == .speaking {
                        context.fill(oval(47, 55, 3.8, 2.3 + 1.2 * wave), with: .color(ink))
                    }
                }
            }
            .shadow(color: .black.opacity(0.35), radius: 8, y: 5)
            .rotationEffect(.degrees(phase == .listening ? -4 + wave : wave * 2))
            .scaleEffect(phase == .listening ? 1.025 + wave * 0.009 : 1)
            .offset(y: wave * (phase == .waiting ? 3 : 2))
        }
    }
}
